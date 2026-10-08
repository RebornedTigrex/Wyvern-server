#pragma once

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace Wyvern::Ui {

    // Свой io_context ведомого UI. Ядро сюда не постит — только насос событий.
    class CliIoContext {
        boost::asio::io_context ioc;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard;
        std::jthread thread;

    public:
        CliIoContext()
            : guard(boost::asio::make_work_guard(ioc))
        {
            thread = std::jthread([this](std::stop_token) {
                ioc.run();
                });
        }

        ~CliIoContext() {
            guard.reset();
            ioc.stop();
        }

        CliIoContext(const CliIoContext&) = delete;
        CliIoContext& operator=(const CliIoContext&) = delete;

        boost::asio::io_context& context() noexcept { return ioc; }

        template<typename F>
        void post(F&& fn) {
            boost::asio::post(ioc, std::forward<F>(fn));
        }
    };

    // Поэтапное выполнение: каждый шаг постится отдельно и сам зовёт next.
    // Шаг не обязан звать next — тогда цепочка встаёт (ждём внешнее событие).
    class StageSequence : public std::enable_shared_from_this<StageSequence> {
    public:
        using Next = std::function<void()>;
        using Step = std::function<void(Next)>;

        explicit StageSequence(boost::asio::io_context& io) : ioc(io) {}

        void add(Step step) { steps.push_back(std::move(step)); }

        void run() { postNext(); }

    private:
        boost::asio::io_context& ioc;
        std::vector<Step> steps;
        std::size_t index{ 0 };

        void postNext() {
            if (index >= steps.size())
                return;
            auto step = steps[index++];
            boost::asio::post(ioc, [self = shared_from_this(), step = std::move(step)] {
                step([self] { self->postNext(); });
                });
        }
    };

}
