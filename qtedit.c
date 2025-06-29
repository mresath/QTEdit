/* IMPORTS */

#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>

#include "core.c"

/* DATA */

struct editorConfig E;

/* INIT */

void init(void) {
    E.cx = 0;
    E.rx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.status[0] = '\0';
    E.statustime = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    E.screenrows -= 2;

    resetScreen();
}

/* MAIN LOOP */

int main(int argc, char *argv[])
{
    enableRawMode();
    init();
   
    if (argc >= 2) {
        eopen(argv[1]);
    }

    setStatusMessage(GUIDE_TEXT);

    while (1)
    {
        refreshScreen();
        processKeypress();
    }

    return 0;
}
