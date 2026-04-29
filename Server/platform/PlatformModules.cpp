#include "PlatformModules.h"

#include "Core.h"

#include "FileCache.h"
#include "RequestHandler.h"
#include "DoSProtectionModule.h"
#include "PlatformRoutesModule.h"
#include "HttpTransportModule.h"
#include "udpTransportModule.h"

void registerWyvernPlatform(Core& core) {
    auto& reg = *core.getModuleRegistry();

    // Регистрация в порядке зависимостей: FileCache (без deps) -> RequestHandler ->
    // DoSProtectionModule, PlatformRoutesModule -> HttpTransportModule.
    // UdpTransportModule не зависит от модулей платформы (общается через EventBus),
    // регистрируем последним, чтобы порядок был очевиден читателю.
    reg.registerModule<FileCache>(core.moduleConfig<FileCache>());
    reg.registerModule<RequestHandler>(core.moduleConfig<RequestHandler>());
    reg.registerModule<DoSProtectionModule>(core.moduleConfig<DoSProtectionModule>());
    reg.registerModule<PlatformRoutesModule>(core.moduleConfig<PlatformRoutesModule>());
    reg.registerModule<HttpTransportModule>(
        core.moduleConfig<HttpTransportModule>(),
        core.ioContext());
    reg.registerModule<UdpTransportModule>(
        core.moduleConfig<UdpTransportModule>(),
        core.ioContext());
}
