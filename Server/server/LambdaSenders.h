#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <functional>  // NEW: для std::function колбека после write

#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class LambdaSenders {
public:
    // Sync версия (остаётся для legacy)
    template<class Stream>
    struct send_lambda {
        Stream& stream_;
        bool& close_;
        beast::error_code& ec_;
        send_lambda(Stream& stream, bool& close, beast::error_code& ec)
            : stream_(stream), close_(close), ec_(ec) {
        }
        template<bool isRequest, class Body, class Fields>
        void operator()(http::message<isRequest, Body, Fields>&& msg) const {
            close_ = msg.need_eof();
            http::serializer<isRequest, Body, Fields> sr{ msg };
            http::write(stream_, sr, ec_);
        }
    };

    // Async версия (обновлена: добавлен колбек для after_write)
    template<class Stream>
    struct async_send_lambda {
        Stream& stream_;
        bool& close_;
        std::function<void(beast::error_code)> after_write_cb_;  // NEW: колбек после write (для рекурсии или close)

        async_send_lambda(Stream& stream, bool& close, std::function<void(beast::error_code)> cb = {})
            : stream_(stream), close_(close), after_write_cb_(cb) {
        }

        template<bool isRequest, class Body, class Fields>
        void operator()(http::message<isRequest, Body, Fields>&& msg) const {
            close_ = msg.need_eof();  // true если explicit close
            auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(std::move(msg));
            http::async_write(
                stream_,
                *sp,
                [this, sp, close_ptr = &close_](beast::error_code ec, std::size_t bytes) {  // NEW: Log bytes
                    if (after_write_cb_) {
                        after_write_cb_(ec);
                    }
                    if (!ec && *close_ptr) {
                        // FIXED: Half-close (shutdown_send) — client reads response, но no more writes
                        beast::error_code sec;
                        beast::get_lowest_layer(stream_).shutdown(net::socket_base::shutdown_send, sec);
                    }
                    //std::cout << "Wrote " << bytes << " bytes, close=" << *close_ptr << std::endl;  // Debug log
                });
        }
    };
};