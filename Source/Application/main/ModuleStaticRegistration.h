#pragma once

namespace Wyvern{
    class Core;
}
// Registers all modules of the P2P messenger application into Core.
// Registration order respects dependency declarations (earlier modules have
// no dependencies on later ones).
void registerPlatformModules(Wyvern::Core& core);
