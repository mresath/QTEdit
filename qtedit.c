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

void init(void)
{
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
    E.syntax = NULL;
    E.sel_active = 0;
    E.sel_start_cx = 0;
    E.sel_start_cy = 0;
    E.sel_end_cx = 0;
    E.sel_end_cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    E.screenrows -= 2;

    resetScreen();
}

/* MAIN LOOP */

int main(int argc, char *argv[])
{
    if (argc >= 2)
    {
        for (int i = 1; i < argc; i++)
        {
            char *arg = argv[i];
            if (arg[0] == '-')
            {
                if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
                {
                    fprintf(stderr, "Usage: %s [options] <filename>\n", argv[0]);
                    fprintf(stderr, "Options:\n");
                    fprintf(stderr, "  -h, --help       Show this help message\n");
                    exit(0);
                }
                else
                {
                    fprintf(stderr, "Unknown option: %s\n", arg);
                    fprintf(stderr, "Use -h or --help for usage information.\n");
                    exit(1);
                }
            }
        }
        init();
        enableRawMode();

        eopen(argv[argc - 1]);
    }
    else
    {
        init();
        enableRawMode();
    }

    setStatusMessage(GUIDE_TEXT);

    while (1)
    {
        refreshScreen();
        processKeypress();
    }

    return 0;
}
