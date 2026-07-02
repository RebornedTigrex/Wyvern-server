#include "P2pPlatformModules.h"

#include "Core.h"

// Transport layer
#include "p2p/transport/P2pConnectionModule.h"

// Python runtime + crypto layer
#include "p2p/crypto/PythonRuntimeModule.h"
#include "p2p/crypto/MeshCryptoModule.h"

// DB layer
#include "p2p/db/MeshNodeDbModule.h"

// Business logic
#include "p2p/app/P2pMessengerModule.h"

// Relay signaling (NAT traversal)
#include "p2p/relay/RelaySignalingModule.h"

// Core console
#include "modules/console/InteractiveConsoleModule.h"

void registerP2pMessengerPlatform(Core& core) {
    auto& reg = *core.getModuleRegistry();

    // Registration order: dependencies before dependents.
    // ModuleRegistry enforces the dependency graph, so the order below
    // only needs to ensure each dep is registered before the module that
    // declares it.

    // 1. Python runtime (no dependencies).
    reg.registerModule<PythonRuntimeModule>(
#if defined(PYTHON_HOME_PATH)//Отключаем параметры конфига для него в дебаг режиме: Не нужно, ибо путь будет браться от проекта через параметры сборки
    );
#else
        core.moduleConfig<PythonRuntimeModule>());
#endif
    // 2. Transport (no Core-module dependencies; uses EventBus singleton).
    reg.registerModule<P2pConnectionModule>(
        core.moduleConfig<P2pConnectionModule>(),
        core.ioContext());

    // 3. Crypto facade (depends on PythonRuntimeModule).
    reg.registerModule<MeshCryptoModule>(
        core.moduleConfig<MeshCryptoModule>());

    // 4. DB facade (depends on MeshCryptoModule).
    reg.registerModule<MeshNodeDbModule>(
        core.moduleConfig<MeshNodeDbModule>());

    // 5. Messenger business logic
    //    (depends on P2pConnectionModule, MeshCryptoModule, MeshNodeDbModule).
    reg.registerModule<P2pMessengerModule>(
        core.moduleConfig<P2pMessengerModule>(),
        core.ioContext());

    // 5.5. Relay signaling client — WebSocket to relay server for NAT fallback
    //      (standalone, uses EventBus for coordination).
    reg.registerModule<Wyvern::P2P::Relay::RelaySignalingModule>(
        core.moduleConfig<Wyvern::P2P::Relay::RelaySignalingModule>(),
        core.ioContext());

    // 6. Interactive console — registered last so 'list' shows all modules.
    reg.registerModule<InteractiveConsoleModule>(
        core.moduleConfig<InteractiveConsoleModule>(),
        reg,
        core.ioContext());
}
