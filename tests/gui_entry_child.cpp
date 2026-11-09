#include <windows.h>
#include <cstdio>
#include <cstring>
int game_main(int argc,char **argv) {
    if(GetConsoleWindow()) return 31;
    if(argc!=2 || std::strcmp(argv[1],"argument with spaces")) return 32;
    std::puts("GUI stdout preserved");
    std::fputs("GUI stderr preserved\n",stderr);
    return 7;
}
