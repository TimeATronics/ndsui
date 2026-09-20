// Launches a game by running its stock launch script with the ROM path.
#pragma once

#include <string>

#include "core/Systems.h"

namespace ndsui {

// Runs `<emus>/<system.id>/<script> <game.path>` in a shell and waits for it.
// Returns the shell exit status (0 = success).
int launchGame(const System& system, const Game& game, const LaunchOption& opt);

}  // namespace ndsui
