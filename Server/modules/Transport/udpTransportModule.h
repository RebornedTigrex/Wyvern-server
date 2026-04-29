#pragma once

#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"
#include "contracts/IEventBus.h"
#include "managers/EventBus.h"
#include "UdpPacket.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/post.hpp>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// UdpTransportModule — низкоуровневый UDP I/O платформы Wyvern.
//
// Обязанности (и только они):
//  - открыть UDP-сокет на сконфигурированном endpoint, dual-stack по умолчанию;
//  - читать датаграммы async-loop-ом на общем io_context Core;
//  - валидировать общий бинарный заголовок (см. UdpPacket.h);
//  - публиковать UdpPacketReceived / UdpPacketDropped через EventBus;
//  - принимать UdpSendRequested через EventBus и асинхронно отправлять
//    (single-flight, bounded queue с backpressure-дропом);
//  - вести внутреннюю статистику счётчиков (rx/tx/drops).
//
// Чего модуль НЕ делает (по контракту):
//  - не интерпретирует поле type[1] (справочник типов — задача верхнего слоя);
//  - не реализует ARQ/ACK/sequence/фрагментацию/таймеры/ретрансмиты;
//  - не знает про шифрование, сессии, бизнес-логику звонков/файлов;
//  - не общается с агентами/feature-managers напрямую.
// ---------------------------------------------------------------------------
class UdpTransportModule : public BaseModule {
public:
    static std::string moduleType() { return "wyvern.udpTransport"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        obj["address"]          = "::";        // dual-stack: слушаем оба семейства одним сокетом
        obj["port"]             = 9000;
        obj["recvBufferBytes"]  = 65536;       // SO_RCVBUF; запас под burst
        obj["maxDatagramBytes"] = 1400;        // запас под IPv6 + опции, отсекает IP-фрагментацию
        obj["txQueueLimit"]     = 1024;        // bounded queue для backpressure-дропа
        obj["dualStackV6Only"]  = false;       // true => поведение чистого IPv6 (фоллбек)
        return obj;
    }

    UdpTransportModule(const core::runtime::ConfigSection& cfg,
                       boost::asio::io_context&            ioContext)
        : BaseModule("UDP Transport"),
          ioContext_(ioContext),
          address_(cfg.value<std::string>("address", "::")),
          port_(static_cast<std::uint16_t>(cfg.value<int>("port", 9000))),
          recvBufferBytes_(static_cast<std::size_t>(cfg.value<int>("recvBufferBytes", 65536))),
          maxDatagramBytes_(static_cast<std::size_t>(cfg.value<int>("maxDatagramBytes", 1400))),
          txQueueLimit_(static_cast<std::size_t>(cfg.value<int>("txQueueLimit", 1024))),
          v6Only_(cfg.value<bool>("dualStackV6Only", false)) {}

    std::string moduleKey() const override { return moduleType(); }

    // Снимок счётчиков транспорта; берётся под мьютексом, дёшево.
    struct Stats {
        std::uint64_t rxOk                          = 0;
        std::uint64_t rxBytesOk                     = 0;
        std::uint64_t rxDroppedInvalidMagic         = 0;
        std::uint64_t rxDroppedUnsupportedVersion   = 0;
        std::uint64_t rxDroppedTruncatedHeader      = 0;
        std::uint64_t rxDroppedLengthMismatch       = 0;
        std::uint64_t rxDroppedOversized            = 0;
        std::uint64_t rxSocketErrors                = 0;
        std::uint64_t txOk                          = 0;
        std::uint64_t txBytesOk                     = 0;
        std::uint64_t txSocketErrors                = 0;
        std::uint64_t txDroppedBackpressure         = 0;
    };

    Stats stats() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_;
    }

protected:
    bool onInitialize() override {
        try {
            eventBus_ = EventBus::instance();
            if (!eventBus_) {
                std::cerr << "[UdpTransport] EventBus is not available\n";
                return false;
            }

            // Один сокет на оба семейства: открываем v6 и снимаем V6_ONLY,
            // если оператор не запросил чистый IPv6 явно.
            socket_ = std::make_unique<udp_socket>(ioContext_);
            socket_->open(boost::asio::ip::udp::v6());
            socket_->set_option(boost::asio::ip::v6_only(v6Only_));
            socket_->set_option(boost::asio::socket_base::receive_buffer_size(
                static_cast<int>(recvBufferBytes_)));

            const auto bindAddress = boost::asio::ip::make_address(address_);
            socket_->bind(udp_endpoint{bindAddress, port_});

            rxBuffer_ = std::make_shared<std::vector<std::uint8_t>>();
            // Заводим буфер с запасом на 1 байт сверх лимита: если придёт
            // что-то больше maxDatagramBytes, async_receive_from сообщит ошибку
            // или выдаст полный размер, и мы корректно дропнем такой пакет.
            rxBuffer_->resize(maxDatagramBytes_ + 1);

            // Явный std::function вынуждает компилятор вывести тип события из сигнатуры,
            // обходя парсинг шаблонного метода базы через shared_ptr (МSVC иначе путается).
            std::function<void(const wyvern::transport::udp::UdpSendRequested&)> handler =
                [this](const wyvern::transport::udp::UdpSendRequested& req) {
                    onSendRequested(req);
                };
            sendSubscription_ = eventBus_->subscribe(std::move(handler));

            std::cout << "[UdpTransport] listening on "
                      << bindAddress.to_string() << ':' << port_
                      << (v6Only_ ? " (v6-only)" : " (dual-stack)") << '\n';

            doReceive();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[UdpTransport] init error: " << e.what() << '\n';
            socket_.reset();
            rxBuffer_.reset();
            return false;
        }
    }

    void onShutdown() override {
        if (eventBus_ && sendSubscription_ != 0) {
            eventBus_->unsubscribe(sendSubscription_);
            sendSubscription_ = 0;
        }

        if (socket_) {
            boost::system::error_code ec;
            if (socket_->is_open()) {
                socket_->cancel(ec);
                socket_->close(ec);
            }
            socket_.reset();
        }

        std::lock_guard<std::mutex> lock(txMutex_);
        txQueue_.clear();
        txInFlight_ = false;
        rxBuffer_.reset();
        eventBus_.reset();
    }

private:
    using udp_socket   = boost::asio::ip::udp::socket;
    using udp_endpoint = boost::asio::ip::udp::endpoint;

    // Один элемент TX-очереди — уже сериализованная датаграмма + цель.
    // Буфер держим в shared_ptr, чтобы пережил время жизни async_send_to
    // через захват в лямбде колбэка.
    struct TxItem {
        udp_endpoint                                target;
        std::shared_ptr<std::vector<std::uint8_t>>  datagram;
    };

    // ---- RX ----------------------------------------------------------------
    void doReceive() {
        if (!socket_ || !socket_->is_open() || !rxBuffer_) return;

        socket_->async_receive_from(
            boost::asio::buffer(*rxBuffer_),
            rxRemote_,
            [this](const boost::system::error_code& ec, std::size_t bytes) {
                onReceive(ec, bytes);
            });
    }

    void onReceive(const boost::system::error_code& ec, std::size_t bytes) {
        if (ec == boost::asio::error::operation_aborted) {
            return; // плановый shutdown
        }
        if (ec) {
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.rxSocketErrors += 1;
            }
            std::cerr << "[UdpTransport] receive error: " << ec.message() << '\n';
            // Пытаемся продолжить приём, если сокет ещё открыт.
            if (socket_ && socket_->is_open()) doReceive();
            return;
        }

        const auto sender = endpointToPacket(rxRemote_);

        if (bytes > maxDatagramBytes_) {
            publishDrop(sender, wyvern::transport::udp::DropReason::OversizedDatagram, bytes);
            doReceive();
            return;
        }

        wyvern::transport::udp::PacketHeader header{};
        wyvern::transport::udp::DropReason   reason{};
        const bool ok = wyvern::transport::udp::parseHeader(
            rxBuffer_->data(), bytes, header, reason);

        if (!ok) {
            publishDrop(sender, reason, bytes);
            doReceive();
            return;
        }

        // Успех: формируем событие, перенося payload move-семантикой.
        wyvern::transport::udp::UdpPacketReceived ev;
        ev.sender       = sender;
        ev.type         = header.type;
        ev.flags        = header.flags;
        ev.meta         = header.meta;
        ev.receivedAtNs = nowSteadyNs();

        const std::size_t headerSize = wyvern::transport::udp::kHeaderSize;
        ev.payload.assign(
            rxBuffer_->data() + headerSize,
            rxBuffer_->data() + bytes);

        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.rxOk      += 1;
            stats_.rxBytesOk += bytes;
        }

        eventBus_->publish(std::move(ev));

        doReceive();
    }

    // ---- TX ----------------------------------------------------------------
    void onSendRequested(const wyvern::transport::udp::UdpSendRequested& req) {
        if (!socket_ || !socket_->is_open()) {
            return;
        }

        // Резолвим адрес заранее, чтобы при ошибке не платить за post в io_context.
        boost::system::error_code ec;
        const auto address = boost::asio::ip::make_address(req.target.address, ec);
        if (ec) {
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.txSocketErrors += 1;
            }
            std::cerr << "[UdpTransport] invalid target address '" << req.target.address
                      << "': " << ec.message() << '\n';
            return;
        }

        if (req.payload.size() + wyvern::transport::udp::kHeaderSize > maxDatagramBytes_) {
            // Слишком большая полезная нагрузка — отбрасываем как backpressure-аналог.
            publishDrop(req.target,
                        wyvern::transport::udp::DropReason::OversizedDatagram,
                        req.payload.size() + wyvern::transport::udp::kHeaderSize);
            return;
        }

        // Сериализуем в свой буфер, чтобы вызывающий мог свободно владеть payload.
        auto datagram = std::make_shared<std::vector<std::uint8_t>>();
        datagram->resize(wyvern::transport::udp::kHeaderSize + req.payload.size());
        wyvern::transport::udp::writeHeader(
            datagram->data(),
            req.type,
            req.flags,
            static_cast<std::uint16_t>(req.payload.size()),
            req.meta);
        if (!req.payload.empty()) {
            std::memcpy(
                datagram->data() + wyvern::transport::udp::kHeaderSize,
                req.payload.data(),
                req.payload.size());
        }

        TxItem item;
        item.target   = udp_endpoint{address, req.target.port};
        item.datagram = std::move(datagram);

        // Постим в io_context: безопасно из любого потока (EventBus может вызвать
        // обработчик из контекста publisher-а).
        boost::asio::post(ioContext_,
            [this, it = std::move(item)]() mutable {
                enqueueTx(std::move(it));
            });
    }

    void enqueueTx(TxItem item) {
        bool startSend = false;
        {
            std::lock_guard<std::mutex> lock(txMutex_);
            if (txQueue_.size() >= txQueueLimit_) {
                {
                    std::lock_guard<std::mutex> sl(statsMutex_);
                    stats_.txDroppedBackpressure += 1;
                }
                publishDrop(endpointToPacket(item.target),
                            wyvern::transport::udp::DropReason::TxQueueOverflow,
                            item.datagram->size());
                return;
            }
            txQueue_.push_back(std::move(item));
            if (!txInFlight_) {
                txInFlight_ = true;
                startSend = true;
            }
        }
        if (startSend) doSend();
    }

    void doSend() {
        // Берём голову очереди под мьютексом, отправляем без удержания мьютекса.
        TxItem next;
        {
            std::lock_guard<std::mutex> lock(txMutex_);
            if (txQueue_.empty() || !socket_ || !socket_->is_open()) {
                txInFlight_ = false;
                return;
            }
            next = std::move(txQueue_.front());
            txQueue_.pop_front();
        }

        auto datagram = next.datagram;
        socket_->async_send_to(
            boost::asio::buffer(*datagram),
            next.target,
            [this, datagram](const boost::system::error_code& ec, std::size_t bytes) {
                onSendComplete(ec, bytes);
            });
    }

    void onSendComplete(const boost::system::error_code& ec, std::size_t bytes) {
        if (ec) {
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.txSocketErrors += 1;
            }
            if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "[UdpTransport] send error: " << ec.message() << '\n';
            }
        } else {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.txOk      += 1;
            stats_.txBytesOk += bytes;
        }

        // Идём за следующим элементом, не сбрасывая single-flight флаг,
        // если очередь не пуста.
        bool hasMore = false;
        {
            std::lock_guard<std::mutex> lock(txMutex_);
            if (txQueue_.empty() || !socket_ || !socket_->is_open()) {
                txInFlight_ = false;
            } else {
                hasMore = true;
            }
        }
        if (hasMore) doSend();
    }

    // ---- helpers -----------------------------------------------------------
    void publishDrop(const wyvern::transport::udp::PacketEndpoint& sender,
                     wyvern::transport::udp::DropReason             reason,
                     std::size_t                                    size) {
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            using R = wyvern::transport::udp::DropReason;
            switch (reason) {
                case R::InvalidMagic:        stats_.rxDroppedInvalidMagic        += 1; break;
                case R::UnsupportedVersion:  stats_.rxDroppedUnsupportedVersion  += 1; break;
                case R::TruncatedHeader:     stats_.rxDroppedTruncatedHeader     += 1; break;
                case R::LengthMismatch:      stats_.rxDroppedLengthMismatch      += 1; break;
                case R::OversizedDatagram:   stats_.rxDroppedOversized           += 1; break;
                case R::TxQueueOverflow:     /* учитывается в enqueueTx */            break;
                case R::SocketError:         /* учитывается в onReceive/onSend */     break;
            }
        }

        if (!eventBus_) return;
        wyvern::transport::udp::UdpPacketDropped ev;
        ev.sender = sender;
        ev.reason = reason;
        ev.size   = size;
        ev.atNs   = nowSteadyNs();
        eventBus_->publish(std::move(ev));
    }

    static wyvern::transport::udp::PacketEndpoint endpointToPacket(const udp_endpoint& ep) {
        wyvern::transport::udp::PacketEndpoint out;
        try {
            out.address = ep.address().to_string();
        } catch (...) {
            out.address.clear();
        }
        out.port = ep.port();
        return out;
    }

    static std::int64_t nowSteadyNs() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    }

    // ---- state -------------------------------------------------------------
    boost::asio::io_context& ioContext_;

    std::string                 address_;
    std::uint16_t               port_;
    std::size_t                 recvBufferBytes_;
    std::size_t                 maxDatagramBytes_;
    std::size_t                 txQueueLimit_;
    bool                        v6Only_;

    std::unique_ptr<udp_socket>                 socket_;
    udp_endpoint                                rxRemote_;
    std::shared_ptr<std::vector<std::uint8_t>>  rxBuffer_;

    // Храним указатель на интерфейс базы: в EventBus override оверрайдят type-erased методы и
    // скрывают шаблонные publish<T>/subscribe<T> базы (name hiding).
    // Через указатель на IEventBus шаблонные перегрузки видны напрямую.
    std::shared_ptr<core::contracts::IEventBus> eventBus_;
    core::contracts::SubscriptionId             sendSubscription_ = 0;

    std::mutex                                  txMutex_;
    std::deque<TxItem>                          txQueue_;
    bool                                        txInFlight_ = false;

    mutable std::mutex                          statsMutex_;
    Stats                                       stats_{};
};
