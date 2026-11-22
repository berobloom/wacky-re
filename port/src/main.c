/* main.c: the port's entry. Starts the emulated machine, maps the game's data object from the
 * player's WW.EXE, then runs the game's own main() (src/ww.c, renamed game_main at compile time).
 *
 *   wacky [-C gamedir] [--headless] [--scale N] [game arguments...]
 *
 * gamedir holds the player's Wacky Wheels files (WW.EXE, WACKY.DAT, ...); the game opens them by
 * their DOS names, relative, and writes its config and scores there. Never point it at original/. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "machine.h"

int game_main(int argc, char **argv);

int main(int argc, char **argv)
{
    const char *dir = ".";
    int headless = getenv("PORT_HEADLESS") != NULL, scale = 3, i, n = 1;
    char **gargv = calloc(argc + 1, sizeof *gargv);

    setvbuf(stdout, NULL, _IONBF, 0);  /* the game's messages, in order with ours on stderr */
    gargv[0] = "WW.EXE";
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-C") && i + 1 < argc)
            dir = argv[++i];
        else if (!strcmp(argv[i], "--headless"))
            headless = 1;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else
            gargv[n++] = argv[i];
    }
    if (chdir(dir) != 0) {
        perror(dir);
        return 1;
    }
    if (image_load("WW.EXE") != 0 || textmode_map() != 0)
        return 1;
    if (machine_start(headless, scale) != 0)
        port_fatal("cannot start the hardware thread");
    return game_main(n, gargv);
}
