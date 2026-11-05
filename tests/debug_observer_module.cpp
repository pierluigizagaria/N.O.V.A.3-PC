#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
extern "C" __declspec(dllexport) int fixture_value() { return 73; }
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID) { return TRUE; }
