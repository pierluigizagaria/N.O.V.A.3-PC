#pragma once
#include "launcher/setup.h"
#include <windows.h>
namespace nova::launcher {
// Caller owns the returned process handle. The GUI closes before waiting for it.
HANDLE start_game(const Layout &,const fs::path &executable);
}
