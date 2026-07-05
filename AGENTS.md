# Wyvern-server - контекст для агентов
Короткая шпаргалка, чтобы понимать проект без дорогого анализа. Глубокие детали - через Graphify (см. ниже).
## О проекте
- Wyvern-server - C++20 P2P mesh-мессенджер (узел/сервер сети).
- Точка входа: `Source/Application/main/P2pMessengerMain.cpp`.
- Поток запуска: `Core::instance()` -> `bootstrap(argc, argv)` (CLI `--config|-c`, загрузка конфига, `io_context`) -> `registerP2pMessengerPlatform(core)` -> `initializeModules()` -> `readyModules()` -> `commitConfig()` -> `ioContext().run()` -> `shutdown()`.
## Карта репозитория
- `Source/` - весь исходный код и точки сборки.
  - `Source/Core/` - ядро-фреймворк (пока не трогать, см. подсказки).
  - `Source/Application/` - прикладные модули: `modules/p2p/{transport,crypto,db,app,relay}`, `modules/Transport/udp`, `modules/dataStorage`, `relay/`, `main/`.
  - `Source/Tests/` - тесты на Boost.Test (`Tests/p2p`).
- `cmake/` - модули CMake (`DetectSystemParams`, `BootstrapPythonSDK`, `BootstrapPythonRuntime`).
- `EXTERNALS/` - сабмодули и кэш зависимостей (pybind11 через FetchContent), сюда же кладётся встроенный Python.
- `build/` - артефакты сборки (в `.gitignore`).
- ВАЖНО: `CMakePresets.json` и `CMakeLists.txt` лежат в `Source/`, а не в корне репозитория.
## Стек и сборка
- C++20, CMake (>= 4.2) + Ninja, тулчейн vcpkg (`$VCPKG_ROOT`).
- Зависимости: Boost (`system`, `thread`, `unit_test_framework`), встроенный Python 3.12 через pybind11 (модули crypto/db).
- Конфиг-пресеты (в `Source/`): `windows-build-VS2026` (генератор Visual Studio 18 2026), `windows-build-VS2022` (VS 17 2022), базовый `windows-build` (Ninja).
- Build-пресеты: `windows-release`, `windows-debug`, `windows-release-VS2026`, `windows-debug-VS2026`. Test-пресет: `windows-release` (ctest, фильтр по метке `NoAssets`).
- Цели: `p2p_messenger` (основная), `p2p_tests` (тесты). Переключатели `WYVERN_BUILD_P2P_MESSENGER`, `WYVERN_BUILD_P2P_TESTS`.
- Сборка из каталога `Source/`: `cmake --preset windows-build-VS2026`, затем `cmake --build --preset windows-debug-VS2026`.
## Архитектура (термины)
- `Core` (singleton) владеет: `EventBus`, `ModuleRegistry`, `ConfigStore`, `RuntimeServices` (в т.ч. общий `io_context`).
- Контракты (`Source/Core/contracts`): `IModule`, `IAgent`, `IAction`, `IFeatureManager`, `IEventBus`, `IMessage`, `IModuleRegistry`.
- Базовые классы-расширения: `BaseModule`, `BaseAgent`, `BaseFeatureManager`, `BaseAction`.
- Модель Action-first: `Agent` регистрирует `Action`-ы, сообщения маршрутизируются через `MessageDispatcher`/`ActionRouter` (`MessageRoute` = agent + action). Модули одного уровня общаются через события `EventBus` (напр. `RelayConnectedEvent`, `RelayDataReceivedEvent`, `RelayRendezvousResponseEvent`).
- Жизненный цикл модуля: `initialize` -> `ready` -> `shutdown`, конфиг-секции через `Core::moduleConfig<T>()` (ключ `T::moduleType()`, дефолты `T::defaults()`).
- Ключевые модули (по связности графа): `P2pConnectionModule`, `UdpTransportModule` (+ `UdpPacket`, `OverlayId`), `P2pMessengerModule`, `RelaySignalingModule`/`RelayServer`, `MeshCryptoModule` (Python), `MeshNodeDbModule`/`INodeStore`, `InMemoryRelayStore`, `InteractiveConsoleModule`.
## Подсказки для агентов
1. Все нужные пресеты и стартовые листы (`CMakePresets.json`, `CMakeLists.txt`) лежат в `Source/` - конфигурируй и собирай оттуда, не из корня.
2. `Source/Core/` пока не трогать (стабильное ядро). Прикладную логику добавляй в `Source/Application/`.
## Рабочий цикл (Loop Engineering)
Контекст держим вне памяти агента - в двух файлах:
- `.agents/loop/plan.md` - источник задач (что делать, приоритеты, Definition of Done).
- `.agents/loop/status.md` - журнал: что сделано, где возникли ошибки, что проверить дальше.
Цикл каждой сессии:
1. Прочитай `AGENTS.md` (этот файл) для контекста.
2. Возьми следующую задачу из `.agents/loop/plan.md`.
3. Выполни её по протоколу ниже (уточнение -> реализация -> чекпоинты).
4. Обнови `.agents/loop/status.md` (прогресс, ошибки, следующий шаг).
## Углублённый анализ (Graphify)
Для глубокого понимания кода есть граф проекта (Graphify):
- Построить/обновить граф (код-онли, без API-ключа): `graphify update Source`.
- Полное извлечение (нужен LLM-ключ для доков): `graphify extract Source`.
- Экспорт call-flow (Mermaid, HTML): `graphify export callflow-html`.
- Запрос к графу: `graphify query "вопрос"`. Вывод - в `Source/graphify-out/` (`graph.json`, `GRAPH_REPORT.md`, `graph.html`).
- Каталог `graphify-out/` регенерируемый и в `.gitignore` - не коммить.
# Project Rules
- Перед любой реализацией изменений сначала явно уточни, что именно хочет пользователь (цель, ограничения, ожидаемая архитектура) и получи подтверждение.
- Не начинай реализацию, пока пользователь не подтвердил, что понимание задачи верное.
- Проект считается прототипом: легаси-артефакты и временные обходы не сохраняются, если нет отдельного согласования.
- Не переноси автоматически проектные правила/контекст из других репозиториев без явной адаптации под текущий прототип.
- Не добавляй и не поддерживай обратную совместимость с устаревшими (legacy) ветками кода, если пользователь явно не попросил об этом.
## Протокол постановки задач (anti-misunderstanding)
- Работай в двух фазах: 1) уточнение и фиксация контракта, 2) реализация.
- В фазе уточнения запрещены изменения файлов, запуск мутационных команд и любые реализации.
- Перед реализацией всегда верни контракт задачи в явном виде:
  - цель;
  - границы (что можно менять, что нельзя);
  - критерии готовности (Definition of Done);
  - проверки/валидация;
  - риски и спорные допущения.
- Если есть неоднозначность (две и более разумные трактовки), обязательно задай уточняющие вопросы и не начинай реализацию.
- Если пользователь дал неполную задачу, сначала преобразуй её в Task Card и запроси подтверждение.
- Реализация разрешена только после явного подтверждения пользователя (например: "ОК", "подтверждаю", "делай").

## Чекпоинты выполнения
- После подтверждения веди задачу по чекпоинтам:
  - C1: краткий план и список затрагиваемых модулей;
  - C2: выполнение изменений;
  - C3: проверка (сборка/тесты/валидация);
  - C4: итог (что сделано, что проверено, что осталось).
- Если в ходе C2/C3 появляется новое допущение, вернись в фазу уточнения и запроси повторное подтверждение.

## Task Card (обязательные поля)
- Тип задачи: bugfix | refactor | feature | test
- Цель
- Границы изменений
- Запреты
- Критерии готовности
- Минимальная проверка
