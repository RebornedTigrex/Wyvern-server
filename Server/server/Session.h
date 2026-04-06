#pragma once

#include "RequestHandler.h"
#include "LambdaSenders.h"

#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace fs = std::filesystem;
namespace beast = boost::beast;
namespace http = beast::http;

// UPDATED: Session с shared_ptr для sender lifetime
class session : public std::enable_shared_from_this<session> {
public:
    session(tcp::socket socket, RequestHandler* module)
        : socket_(std::move(socket)), module_(module), close_(false) {
    }

    void run() {
        try {
            do_read();
        }
        catch (const std::exception& e) {
            std::cerr << "Session run error: " << e.what() << std::endl;
            beast::error_code ec;
            beast::get_lowest_layer(socket_).shutdown(net::socket_base::shutdown_both, ec);
        }
    }

private:
    void do_read() {
        req_ = {};
        buffer_.consume(buffer_.size());
        http::async_read(socket_, buffer_, req_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes) {  // NEW: дебаг байты
                if (!ec) {
                    //std::cout << "Read " << bytes << " bytes for next request" << std::endl;  // Debug: keep-alive reads
                    self->on_read();
                }
                else if (ec == http::error::end_of_stream) {
                    //std::cout << "End of stream — closing session" << std::endl;
                    // Graceful close
                    beast::error_code sec;
                    self->socket_.shutdown(net::socket_base::shutdown_both, sec);
                }
                else {
                    std::cerr << "Read error (" << bytes << " bytes): " << ec.message() << std::endl;
                    beast::error_code sec;
                    beast::get_lowest_layer(self->socket_).shutdown(net::socket_base::shutdown_both, sec);
                }
            });
    }

    void on_read() {
        // FIXED: make_shared без {} — используем default cb в ctor
        auto sp_sender = std::make_shared<LambdaSenders::async_send_lambda<tcp::socket>>(socket_, close_);
        auto sender_ref = std::ref(*sp_sender);  // Ref to deref sp_sender (valid)

        // Лямбда для after_write — захват sp_sender (copy shared) + self (no dangling)
        auto after_write = [self = shared_from_this(), sp_sender](beast::error_code ec) {
            if (ec == http::error::end_of_stream) {  // NEW: Client closed — normal, no re-read
                //std::cout << "Client closed connection gracefully" << std::endl;
                return;
            }
            if (!ec && !sp_sender->close_) {
                self->do_read();  // Keep-alive
            }
            else if (ec) {
                std::cerr << "Post-write error: " << ec.message() << std::endl;
            }
            };

        // FIXED: Set cb ПОСЛЕ создания sp_sender, но ДО handleRequest
        sp_sender->after_write_cb_ = after_write;

        // Теперь handleRequest: sender живёт via sp, ref ok
        module_->handleRequest(std::move(req_), sender_ref);
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    RequestHandler* module_;
    bool close_;  // Member ok
};