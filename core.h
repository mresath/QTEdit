/* IMPORTS */

#include <termios.h>
#include <time.h>

/* MACROS */

#define VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to get the value of ctrl + some key
#define ABUF_INIT {NULL, 0}      // Empty append buffer
#define TAB_STOP 4               // How many chars each tab is
#define GUIDE "Ctrl-S: Save | Ctrl-X: Quit | Ctrl-F: Find | Ctrl-G: Goto Line | Ctrl-H: Help" // Status message for help
#define QUIT_PROT 3 // Number of times to press Ctrl-X to quit when dirty
#define QUIT_TEXT "WARNING: File has unsaved changes. Press Ctrl-X %d more time%s to quit."

enum keycodes // Codes for break characters
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    DELETE_KEY
};

/* STRUCTS */

typedef struct erow
{
    char *chars;  // Actual content of a row
    char *render; // Whats visible to the user
    int size;
    int rsize;
} erow;

struct editorConfig
{
    int cx, cy; // Where cursor currently is
    int rx;     // Where cursor is visible after render changes
    int screenrows, screencols; // Size of screen
    int numrows; // Rows of text in memory
    int rowoff, coloff; // Offsets of whats currently visible
    erow *row; // Text in memory
    int dirty; // If the file has been modified
    char *filename; // Name of open file
    char status[80]; // Msg show at the bottom
    time_t statustime; // Timestamp of status
    struct termios orig_termios; // Original terminal
};

struct abuf
{ // Append buffer to group up write operations
    char *b;
    int len;
};
