#include "ModuleStaticRegistration.h"

#include "WyvernCore.h"

// Relay signaling (NAT traversal)
#include "p2p/relay/RelaySignalingModule.h"

// Core console
#include "modules/console/InteractiveConsoleModule.h"

void registerPlatformModules(Wyvern::Core& core) {
    auto& reg = *core.getModuleRegistry();

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
