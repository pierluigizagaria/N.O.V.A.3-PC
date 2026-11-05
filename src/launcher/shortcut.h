#pragma once
#include "launcher/setup.h"
namespace nova::launcher {
// Uses local user-owned artwork. Never embeds game files into an executable.
void create_game_shortcut(const Layout &,const fs::path &launcher);
}
