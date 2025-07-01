/* IMPORTS */

#include <termios.h>
#include <time.h>

/* MACROS */

#define CTRL_KEY(k) ((k) & 0x1f) // Macro to get the value of ctrl + some key
#define ABUF_INIT {NULL, 0}      // Empty append buffer
#define TAB_STOP 4               // How many chars each tab is
#define QUIT_PROT 3              // Number of times to press Ctrl-X to quit when dirty

#define VERSION "1.0.2"
#define GUIDE_TEXT "Ctrl-S: Save | Ctrl-X: Quit | Ctrl-F: Find | Ctrl-G: Goto | Ctrl-K: Delete | Ctrl-C/V: Copy/Paste | Ctrl-H: Help" // Status message for help
#define QUIT_TEXT "WARNING: File has unsaved changes. Press Ctrl-X %d more time%s to quit."                                                             // Status message for quit without saving warning
#define FIND_TEXT "Search: %s (Use ESC/Arrows/Enter)"                                                                                                   // Status message for search

enum keycodes // Codes for break characters
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    SHIFT_ARROW_LEFT,
    SHIFT_ARROW_RIGHT,
    SHIFT_ARROW_UP,
    SHIFT_ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    DELETE_KEY,
    SKIP_KEY, // Key that wont be processed
};

enum highlight // Highlight types
{
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEY1,
    HL_KEY2,
    HL_STRING,
    HL_NUMBER,
    HL_FUNC,
    HL_VAR,
    HL_MATCH,
    HL_SELECTION
};

// Highlight flags
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/* STRUCTS */

struct editorSyntax
{
    char *filetype;
    char **filematch;
    char **keywords;
    char *sl_comment_start;
    char *ml_comment_start;
    char *ml_comment_end;
    int flags;
};

typedef struct erow
{
    int index;
    char *chars;       // Actual content of a row
    char *render;      // Whats visible to the user
    unsigned char *hl; // Syntax highlighting
    int size;
    int rsize;
    int hl_open_comment; // If the row has an open comment
} erow;

struct editorConfig
{
    int cx, cy;                  // Where cursor currently is
    int rx;                      // Where cursor is visible after render changes
    int screenrows, screencols;  // Size of screen
    int numrows;                 // Rows of text in memory
    int rowoff, coloff;          // Offsets of whats currently visible
    erow *row;                   // Text in memory
    int dirty;                   // If the file has been modified
    char *filename;              // Name of open file
    char status[200];            // Msg show at the bottom
    time_t statustime;           // Timestamp of status
    struct editorSyntax *syntax; // Syntax for open editor
    struct termios orig_termios; // Original terminal

    // Selection state
    int sel_active;                 // Whether selection is active
    int sel_start_cx, sel_start_cy; // Selection start position
    int sel_end_cx, sel_end_cy;     // Selection end position
};

struct abuf
{ // Append buffer to group up write operations
    char *b;
    int len;
};

/* FILETYPES FOR HL */

/** C **/
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue",
    "enum", "case", "#include", "return", "else", "#define",
    "int|", "long|", "double|", "float|", "char|", "unsigned|",
    "void|", "extern|", "size_t|", "ssize_t|", "static|",
    "struct|", "union|", "class|", "typedef|", "signed|",
    "time_t|", NULL};

/**  JS/TS **/
char *JS_HL_extensions[] = {".js", ".jsx", NULL};
char *JS_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue",
    "case", "return", "else", "import", "from", "export",
    "default", "async", "await", "try", "catch", "finally",
    "function|", "const|", "var|", "class|", "static|",
    "let|", "extends|", "keyof|", "typeof|", "in|",
    "of|", "new|", "this|", NULL};

char *TS_HL_extensions[] = {".ts", ".tsx", NULL};
char *TS_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue",
    "enum", "case", "return", "else", "import", "from", "export",
    "default", "async", "await", "try", "catch", "finally",
    "function|", "string|", "number|", "const|", "var|",
    "interface|", "type|", "class|", "String|", "boolean|",
    "let|", "public|", "extends|", "keyof|", "typeof|", "in|",
    "of|", "new|", "this|", "static|", "private|", NULL};

struct editorSyntax HLDB[] = {
    {"c",
     C_HL_extensions,
     C_HL_keywords,
     "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"javascript",
     JS_HL_extensions,
     JS_HL_keywords,
     "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"typescript",
     TS_HL_extensions,
     TS_HL_keywords,
     "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))
