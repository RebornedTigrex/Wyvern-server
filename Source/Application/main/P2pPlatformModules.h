#pragma once

class Core;

// Registers all modules of the P2P messenger application into Core.
// Registration order respects dependency declarations (earlier modules have
// no dependencies on later ones).
void registerP2pMessengerPlatform(Core& core);
