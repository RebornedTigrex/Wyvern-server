// test_user_io.cpp
#define BOOST_TEST_MODULE UserInputOutputTests
#include <boost/test/unit_test.hpp>
#include <boost/asio/io_context.hpp>


/*
От меня:
    Нейронка здесь знатно насрала, но в теории,
    она задала внешние призники работы модуля.
    Этот модуль - система гарантий доставки сообщений с адекватным ответом
    и гарантией потокобезопасности. Модуль в первую очередь нужен для приятных
    впечатлений использования CLI.
    Сейчас нужно:
    - [] Определиться с поведением модуля окончательно
    - [] Выделить все тесткейсы со всеми состояниями
    - [] Написать testSuite ()
    - [] Отработать все эти тесткейсы

*/

#include "submodules/io/UserInputOutput.h"   // твой заголовок

// Фикстура — создаёт и уничтожает модуль
struct UserIOFixture {
    Console module;

    UserIOFixture() {
        // Можно подготовить ConfigSection / ModuleRegistry, если они нужны
        BOOST_REQUIRE(module.onInitialize());   // или через ModuleRegistry
    }
};

BOOST_FIXTURE_TEST_SUITE(UserIOSuite, UserIOFixture)

BOOST_AUTO_TEST_CASE(module_key_is_correct) {
    BOOST_TEST(module.moduleKey() == "wyvern.userio");
}

BOOST_AUTO_TEST_CASE(send_output_changes_status) {
    UserIODeclaration decl{
        this.id = 1,
        this.sendFrom = "client",
        this.sendTo   = "server",
        this.define   = UserIODefine::NeedRequest,
        this.status   = UserIOStatus::WaitingForRequest
    };

    module.sendOutput("payload", decl);

    // Здесь проверяем наблюдаемое поведение:
    // - либо через onOutput() callback,
    // - либо через публичный getter статуса (его пока нет — добавь в контракт!),
    // - либо через mock/spy на процессор.
}

BOOST_AUTO_TEST_CASE(on_output_returns_callable) {
    auto cb = module.onOutput();
    BOOST_TEST(static_cast<bool>(cb));   // не пустой
    // cb(); // если безопасно
}

BOOST_AUTO_TEST_SUITE_END()