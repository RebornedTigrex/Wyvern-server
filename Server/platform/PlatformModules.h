#pragma once

class Core;

// Регистрирует все встроенные модули платформы Wyvern в указанном Core.
// Это единственное место, где перечислены конкретные модули. Параметры
// каждого модуля приходят из ConfigStore через core.moduleConfig<T>(),
// io_context — из core.ioContext().
void registerWyvernPlatform(Core& core);
