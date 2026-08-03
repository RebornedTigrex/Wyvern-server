#include "StaticModuleRegistration.h"

#include "Core.h"

// Core console
#include "submodules/console/InteractiveConsoleModule.h"
#include "modules/p2p/transport/P2pConnectionModule.h"


void StaticModuleRegistration(Core& core) {
    auto& reg = *core.getModuleRegistry();

    reg.registerModule<P2pConnectionModule>(
        core.moduleConfig<P2pConnectionModule>(),
        core.ioContext()
    );

    reg.registerModule<InteractiveConsoleModule>(
        core.moduleConfig<InteractiveConsoleModule>(),
        core.getModuleRegistry(),
        core.ioContext()
    );
}
