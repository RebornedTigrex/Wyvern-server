#pragma once

#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"
#include "RequestHandler.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace net   = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using     tcp   = net::ip::tcp;

// ---------------------------------------------------------------------------
// HttpSession — приватная единица транспортного уровня.
// Живёт внутри HttpTransportModule и не видна снаружи.
// ---------------------------------------------------------------------------
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, RequestHandler* handler)
        : socket_(std::move(socket))
        , handler_(handler) {}

    void run() { doRead(); }

private:
    void doRead() {
        req_ = {};
        buffer_.consume(buffer_.size());
        http::async_read(socket_, buffer_, req_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (!ec) {
                    self->onRead();
                } else if (ec != http::error::end_of_stream) {
                    std::cerr << "[HttpSession] read error: " << ec.message() << '\n';
                }
            });
    }

    void onRead() {
        auto res = std::make_shared<http::response<http::string_body>>(
            http::status::not_found, req_.version());
        res->set(http::field::server, "Wyvern");
        res->keep_alive(req_.keep_alive());

        RequestHandler::RequestFlowContext ctx;
        beast::error_code endpointEc;
        const auto ep = socket_.remote_endpoint(endpointEc);
        if (!endpointEc) ctx.clientIp = ep.address().to_string();

        handler_->handleRequest(std::move(req_),
            [self = shared_from_this(), res](auto&& response) {
                *res = std::move(response);
                self->doWrite(res);
            },
            ctx);
    }

    void doWrite(std::shared_ptr<http::response<http::string_body>> res) {
        const bool keepAlive = res->keep_alive();
        res->prepare_payload();
        http::async_write(socket_, *res,
            [self = shared_from_this(), res, keepAlive](beast::error_code ec, std::size_t) {
                if (ec) {
                    std::cerr << "[HttpSession] write error: " << ec.message() << '\n';
                    return;
                }
                if (keepAlive) {
                    self->doRead();
                } else {
                    beast::error_code sec;
                    self->socket_.shutdown(tcp::socket::shutdown_send, sec);
                }
            });
    }

    tcp::socket                         socket_;
    beast::flat_buffer                  buffer_;
    http::request<http::string_body>    req_;
    RequestHandler*                     handler_;
};

// ---------------------------------------------------------------------------
// HttpTransportModule — Core v2 модуль.
// Владеет acceptor-ом, запускает async accept loop в onInitialize.
// Зависит от wyvern.requestHandler — получает его через onInject.
// ---------------------------------------------------------------------------
class HttpTransportModule : public BaseModule {
public:
    static std::string moduleType() { return "wyvern.httpTransport"; }
    static boost::json::object defaults() {
        boost::json::object obj;
        obj["address"] = "0.0.0.0";
        obj["port"] = 8080;
        return obj;
    }

    HttpTransportModule(const core::runtime::ConfigSection& cfg, net::io_context& ioContext)
        : BaseModule("HTTP Transport")
        , ioContext_(ioContext)
        , address_(cfg.value<std::string>("address", "0.0.0.0"))
        , port_(static_cast<unsigned short>(cfg.value<int>("port", 8080))) {}

    std::string moduleKey() const override { return moduleType(); }
    std::vector<std::string> dependencies() const override { return {"wyvern.requestHandler"}; }

    void onInject(const std::string& depKey, core::contracts::IModule* dep) override {
        if (depKey == "wyvern.requestHandler")
            handler_ = dynamic_cast<RequestHandler*>(dep);
    }

protected:
    bool onInitialize() override {
        if (!handler_) return false;
        try {
            auto endpoint = tcp::endpoint{net::ip::make_address(address_), port_};
            acceptor_ = std::make_unique<tcp::acceptor>(ioContext_, endpoint);
            std::cout << "[HttpTransport] listening on "
                      << address_ << ':' << port_ << '\n';
            doAccept();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[HttpTransport] init error: " << e.what() << '\n';
            return false;
        }
    }

    void onShutdown() override {
        if (acceptor_ && acceptor_->is_open()) {
            beast::error_code ec;
            acceptor_->close(ec);
        }
    }

private:
    void doAccept() {
        acceptor_->async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    logConnection(socket);
                    std::make_shared<HttpSession>(std::move(socket), handler_)->run();
                } else {
                    std::cerr << "[HttpTransport] accept error: " << ec.message() << '\n';
                }
                if (acceptor_->is_open()) doAccept();
            });
    }

    static void logConnection(const tcp::socket& socket) {
        beast::error_code ec;
        const auto ep = socket.remote_endpoint(ec);
        if (!ec)
            std::cout << "[HttpTransport] connection from "
                      << ep.address().to_string() << ':' << ep.port() << '\n';
    }

    net::io_context&                ioContext_;
    std::string                     address_;
    unsigned short                  port_;
    RequestHandler*                 handler_ = nullptr;
    std::unique_ptr<tcp::acceptor>  acceptor_;
};
