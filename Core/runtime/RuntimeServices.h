#pragma once

#include <boost/asio/io_context.hpp>

namespace core::runtime {

// Контейнер для рантайм-сервисов, владельцем которых является Core.
// На сегодняшний день сюда входит io_context; точка расширения для будущих
// сервисов (executors, timers, signal sets) — сюда же.
struct RuntimeServices {
    boost::asio::io_context ioContext;
};

} // namespace core::runtime
