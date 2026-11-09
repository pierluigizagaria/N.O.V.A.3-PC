#include <windows.h>
#include <cstdlib>

int game_main(int argc, char **argv);

// Use the Windows GUI subsystem from process creation. The CRT still supplies
// the existing argument contract and inherited launcher/diagnostic log handles.
// No AllocConsole/FreeConsole flash, shell wrapper or second rendering process.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return game_main(__argc, __argv);
}
