/* IMPORTS */

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>

#include "core.h"
#include "util.c"

/* DATA */

extern struct editorConfig E;

/* PROTOTYPES */

void setStatusMessage(const char *fmt, ...);

void die(char *s);

void resetScreen(void);

void refreshScreen(void);

char *askPrompt(char *prompt, void (*callback)(char *, int));

void renderRowSyntax(erow *row);

int syntaxToColor(int hl);

void selectSyntax(void);

void scroll(void);

int getWindowSize(int *rows, int *cols);

void clearSelection(void);

void startSelection(void);

void updateSelection(void);

int isPositionSelected(int row, int col);

void deleteSelection(void);

void copySelection(void);

void pasteFromClipboard(void);

/* ROW OPS */

void renderRow(erow *row)
{
    // Changes row rendering for certain characters
    int t = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            t++;

    free(row->render);
    row->render = malloc(row->size + t * (TAB_STOP - 1) + 1);

    int i = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[i++] = ' ';
            while (i % TAB_STOP != 0)
                row->render[i++] = ' ';
        }
        else
        {
            row->render[i++] = row->chars[j];
        }
    }
    row->render[i] = '\0';
    row->rsize = i;

    renderRowSyntax(row);
}

void insertRow(int at, char *s, size_t len)
{
    // Insert row to current text in memory
    if (at < 0 || at > E.numrows)
        return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; j++)
        E.row[j].index++;

    E.row[at].index = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    renderRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

int getCursorRx(erow *row, int cx)
{
    // Calculate where the cursor should be based on rendering changes
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

int getCursorCx(erow *row, int rx)
{
    // Inverse of getCursorRx, calculates the character index from the rendered position
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
            cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
        cur_rx++;
        if (cur_rx > rx)
            return cx;
    }
    return cx;
}

void rowInsertChar(erow *row, int at, char c)
{
    // Insert a character into a row at a specific position
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    renderRow(row);
    E.dirty++;
}

void rowDeleteChar(erow *row, int at)
{
    // Delete a character from a row at a specific position
    if (at < 0 || at >= row->size)
        return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    renderRow(row);
    E.dirty++;
}

void freeRow(erow *row)
{
    // Free the memory allocated for a row
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void deleteRow(int at)
{
    // Delete a row from the text in memory
    if (at < 0 || at >= E.numrows)
        return;
    freeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++)
        E.row[j].index--;
    E.numrows--;
    E.dirty++;
}

void rowAppendString(erow *row, char *s, size_t len)
{
    // Append a string to the end of a row
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    renderRow(row);
    E.dirty++;
}

/* OPS */

void insertChar(int c)
{
    // Insert char at cursor
    int offset = log10(E.numrows) + 2; // Offset for line numbers

    if (E.cy == E.numrows)
    {
        if (E.numrows == 0)
        {
            E.cx = 2;
        }
        insertRow(E.numrows, "", 0);
    }
    rowInsertChar(&E.row[E.cy], E.cx - offset, c);
    E.cx++;
}

void insertNewline(void)
{
    // Enter press basically
    int offset = log10(E.numrows) + 2; // Offset for line numbers

    erow *row = &E.row[E.cy];
    char *indent = malloc(32);
    int i = 0;
    while (row->chars[i] == ' ' || row->chars[i] == '\t')
    {
        indent[i] = row->chars[i];
        i++;
    }

    if (E.cx == offset)
    {
        insertRow(E.cy, "", 0);
    }
    else
    {
        realloc(indent, row->size - E.cx + offset + i);
        memcpy(indent + i, &row->chars[E.cx - offset], row->size - E.cx + offset);
        insertRow(E.cy + 1, indent, row->size - E.cx + offset + i);
        row = &E.row[E.cy];
        row->size = E.cx - offset;
        row->chars[row->size] = '\0';
        renderRow(row);
    }
    E.cy++;
    E.cx = offset + i;
}

void deleteChar(void)
{
    // Delete char at cursor
    int offset = log10(E.numrows) + 2; // Offset for line numbers
    if (E.cy == E.numrows)
        return;
    if (E.cx == offset && E.cy == 0)
        return;
    erow *row = &E.row[E.cy];
    if (E.cx > offset)
    {
        rowDeleteChar(row, E.cx - 1 - offset);
        E.cx--;
    }
    else
    {
        E.cx = E.row[E.cy - 1].size + offset; // Move to end of previous line
        rowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        deleteRow(E.cy);
        E.cy--;
    }
}

void delCurRow(void)
{
    deleteRow(E.cy);
}

void paste(void)
{
}

/* I/O */

void *rowsToString(int *buflen)
{
    // Convert all rows to a single string for saving
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;
    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void eopen(char *filename)
{
    // Open a file and read its contents into memory
    free(E.filename);
    E.filename = strdup(filename);

    selectSyntax();

    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    { // Read file line by line and append to memory
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        insertRow(E.numrows, line, linelen);
    }

    free(line);
    fclose(fp);

    E.cx = log10(E.numrows) + 2; // Set cursor to the start of the first line
    E.dirty = 0;                 // Reset dirty flag
}

void esave(void)
{
    // Save the current text in memory to a file
    if (E.filename == NULL)
    {
        E.filename = askPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL)
        {
            setStatusMessage("Save aborted");
            return;
        }
        selectSyntax();
    }

    int len;
    char *buf = rowsToString(&len);

    int fd = open("qtedit_temp", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd != -1)
    {
        ssize_t res = write(fd, buf, len);
        if (res == len)
        {
            if (rename("qtedit_temp", E.filename) != -1)
            {
                close(fd);
                free(buf);
                setStatusMessage("Saved to %s", E.filename);
                E.dirty = 0; // Reset dirty flag
                return;
            }
        }
        close(fd);
    }

    free(buf);
    setStatusMessage("Error saving to %s: %s", E.filename, strerror(errno));
}

/* SEARCH */

void search(char *query, int key)
{
    static int lm = -1;
    static int dir = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;

    if (saved_hl)
    {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == 'r' || key == '\x1b')
    {
        lm = -1;
        dir = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
    {
        dir = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP)
    {
        dir = -1;
    }
    else
    {
        lm = -1;
        dir = 1;
    }

    if (lm == -1)
        dir = 1;
    int cur = lm;
    int i;
    for (i = 0; i < E.numrows; i++)
    {
        cur += dir;
        if (cur == -1)
            cur = E.numrows - 1;
        else if (cur == E.numrows)
            cur = 0;

        erow *row = &E.row[cur];
        char *match = strstr(row->render, query);
        if (match)
        {
            lm = cur;
            E.cy = cur;
            E.cx = getCursorCx(row, match - row->render + strlen(query)) + log10(E.numrows) + 2;
            E.rowoff = E.numrows;

            saved_hl_line = cur;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void find(void)
{
    int scy = E.cy; // Save current cursor position
    int scx = E.cx;
    int sco = E.coloff;
    int sro = E.rowoff;

    char *query = askPrompt(FIND_TEXT, search);

    if (query)
    {
        free(query);
    }
    else
    {
        // If search was cancelled, restore cursor position
        E.cy = scy;
        E.cx = scx;
        E.coloff = sco;
        E.rowoff = sro;
    }
}

/* APPEND BUFFER */

void abAppend(struct abuf *ab, const char *s, int len)
{
    // Append a string to the append buffer
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    // Free the memory allocated for the append buffer
    free(ab->b);
}

/* TERMINAL */

/** CONFIG **/

void die(char *s)
{
    resetScreen();
    perror(s);
    exit(1);
}

void resetScreen(void)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void disableRawMode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode(void)
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/** INPUT **/

int readKey(void)
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        int or = E.screenrows;
        int oc = E.screencols;
        getWindowSize(&E.screenrows, &E.screencols);
        E.screenrows -= 2;
        if (or != E.screenrows || oc != E.screencols)
        {
            return SKIP_KEY;
        }

        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b') // Escape sequence
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[') // Starts with '['
        {
            if (seq[1] >= '0' && seq[1] <= '9') // Possibly 3+ long sequences
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';

                if (seq[2] == '~') // Ends with '~'
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DELETE_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
                else if (seq[1] == '1' && seq[2] == ';') // Extended sequences like ESC[1;2A
                {
                    char modifier;
                    char direction;
                    if (read(STDIN_FILENO, &modifier, 1) != 1)
                        return '\x1b';
                    if (read(STDIN_FILENO, &direction, 1) != 1)
                        return '\x1b';

                    // Check for Shift modifier (2) and handle direction
                    if (modifier == '2') // Shift modifier
                    {
                        switch (direction)
                        {
                        case 'A':
                            return SHIFT_ARROW_UP;
                        case 'B':
                            return SHIFT_ARROW_DOWN;
                        case 'C':
                            return SHIFT_ARROW_RIGHT;
                        case 'D':
                            return SHIFT_ARROW_LEFT;
                        default:
                            return '\x1b';
                        }
                    }
                    else // Other modifiers - just handle as normal arrows
                    {
                        switch (direction)
                        {
                        case 'A':
                            return ARROW_UP;
                        case 'B':
                            return ARROW_DOWN;
                        case 'C':
                            return ARROW_RIGHT;
                        case 'D':
                            return ARROW_LEFT;
                        default:
                            return '\x1b';
                        }
                    }
                }
            }
            else // 2 long sequences
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O') // Starts with 'O'
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    }

    return c;
}

int getCursorPosition(int *rows, int *cols)
{
    // This function sends an escape sequence to the terminal to request the cursor position
    // and reads the response to determine the current cursor position.
    // The response is expected to be in the format: ESC [ rows ; cols R
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return -0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // If ioctl fails, fallback to using escape sequences
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;

    return 0;
}

char *askPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        setStatusMessage(prompt, buf);
        refreshScreen();

        int c = readKey();
        if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        {
            if (buflen != 0)
                buf[--buflen] = '\0';
        }
        else if (c == '\x1b')
        {
            setStatusMessage("");
            if (callback)
                callback(buf, c);
            free(buf);
            return NULL;
        }
        else if (c == '\r')
        { // Enter key
            if (buflen > 0)
            {
                setStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128)
        {
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback)
            callback(buf, c);
    }
}

void gotoLine(void)
{
    // Function to go to a specific line in the text
    int line = 0;
    char *line_str = askPrompt("Goto line: %s (ESC to cancel)", NULL);
    if (line_str)
    {
        line = atoi(line_str);
        free(line_str);
    }

    if (line > 0 && line <= E.numrows)
    {
        E.cy = line - 1;
        E.cx = log10(E.numrows) + 2;
        scroll();
    }
}

/** LOGIC **/

void moveCursor(int key)
{
    int offset = log10(E.numrows) + 2; // Offset for line numbers

    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key)
    {
    case ARROW_LEFT: // Move left
        if (E.cx > offset)
        {
            E.cx--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size + offset;
        }
        break;
    case ARROW_RIGHT: // Move right
        if (row && E.cx < row->size + offset)
        {
            E.cx++;
        }
        else if (row && E.cx == row->size + offset)
        {
            E.cy++;
            E.cx = offset;
            E.coloff = 0;
        }
        break;
    case ARROW_DOWN: // Move down
        if (E.cy < E.numrows)
            E.cy++;
        break;
    case ARROW_UP: // Move up
        if (E.cy > 0)
            E.cy--;
        break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    rowlen += offset;
    if (E.cx > rowlen)
        E.cx = rowlen;
    if (E.cx < offset)
        E.cx = offset;
}

void moveCursorWithSelection(int key)
{
    // Handle cursor movement with selection
    if (!E.sel_active)
    {
        startSelection();
    }

    // Convert shift+arrow keys to regular arrow keys for movement
    int movement_key;
    switch (key)
    {
    case SHIFT_ARROW_LEFT:
        movement_key = ARROW_LEFT;
        break;
    case SHIFT_ARROW_RIGHT:
        movement_key = ARROW_RIGHT;
        break;
    case SHIFT_ARROW_UP:
        movement_key = ARROW_UP;
        break;
    case SHIFT_ARROW_DOWN:
        movement_key = ARROW_DOWN;
        break;
    default:
        return; // Should not happen
    }

    moveCursor(movement_key);
    updateSelection();
}

void processKeypress(void)
{
    static int quit_count = QUIT_PROT; // Counter for quitting when dirty

    int c = readKey();

    int offset = log10(E.numrows) + 2; // Offset for line numbers

    switch (c)
    {
    case SKIP_KEY:
        return;

    case '\r':
        if (E.sel_active)
        {
            deleteSelection();
        }
        insertNewline();
        break;

    case CTRL_KEY('x'): // Exit on Ctrl-X
        if (E.dirty && --quit_count > 0)
        {
            setStatusMessage(QUIT_TEXT, quit_count, quit_count == 1 ? "" : "s");
            return;
        }
        resetScreen();
        exit(0);
        break;

    case CTRL_KEY('s'): // Save on Ctrl-S
        esave();
        break;

    case CTRL_KEY('g'): // Goto line on Ctrl-G
        gotoLine();
        break;

    case CTRL_KEY('h'): // Help on Ctrl-H
        setStatusMessage(GUIDE_TEXT);
        break;

    case CTRL_KEY('k'):
        delCurRow();
        break;

    case CTRL_KEY('c'): // Copy on Ctrl-C
        copySelection();
        break;

    case CTRL_KEY('v'): // Paste on Ctrl-V
        pasteFromClipboard();
        break;

    case HOME_KEY: // Return to start of line on HOME
        E.cx = offset;
        break;
    case END_KEY: // Skip to end of line on END
        if (E.cy < E.numrows)
        {
            E.cx = E.row[E.cy].size;
        }
        break;

    case CTRL_KEY('f'): // Find on Ctrl-F
        find();
        break;

    case BACKSPACE:
    case DELETE_KEY:
        if (E.sel_active)
        {
            deleteSelection();
        }
        else
        {
            if (c == DELETE_KEY)
                moveCursor(ARROW_RIGHT); // Move right if DELETE is pressed
            deleteChar();
        }
        break;

    case PAGE_UP: // Go to top/bottom of file on PAGE_UP/PAGE_DOWN
    case PAGE_DOWN:
    {
        if (c == PAGE_UP)
        {
            E.cy = E.rowoff;
        }
        else
        {
            E.cy = E.rowoff + E.screenrows + 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }

        int t = E.screenrows;
        while (t--)
            moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_LEFT:      // Move left
    case ARROW_RIGHT:     // Move right
    case ARROW_DOWN:      // Move down
    case ARROW_UP:        // Move up
        clearSelection(); // Clear selection on normal movement
        moveCursor(c);
        break;

    case SHIFT_ARROW_LEFT:  // Move left with selection
    case SHIFT_ARROW_RIGHT: // Move right with selection
    case SHIFT_ARROW_DOWN:  // Move down with selection
    case SHIFT_ARROW_UP:    // Move up with selection
        moveCursorWithSelection(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        if (iscntrl(c) || c >= 127)
        {
            break;
        }
        if (E.sel_active)
        {
            deleteSelection();
        }
        insertChar(c); // Insert character
        break;
    }

    quit_count = QUIT_PROT; // Reset quit count after any keypress
}

/** OUTPUT **/

void scroll(void)
{
    int offset = log10(E.numrows) + 2; // Offset for line numbers

    // Calculates scroll based on cursor position and text content
    E.rx = E.cy < E.numrows ? getCursorRx(&E.row[E.cy], E.cx) : 0;

    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx - offset < E.coloff)
    {
        E.coloff = E.rx - offset;
    }
    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void drawRows(struct abuf *ab)
{
    // Calculates the currently visible rows and appends them to the buffer
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "QTEdit -- version %s", VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            // Start the row with line numbers
            int maxlen = log10(E.numrows) + 2;
            char starttext[8];
            int nlen = sprintf(starttext, "%d ", filerow + 1);
            char *start = malloc(maxlen + 1);
            char *sp = start;
            for (int i = 0; i < maxlen; i++)
            {
                if (i < nlen)
                    *sp++ = starttext[i];
                else
                    *sp++ = ' ';
            }
            abAppend(ab, start, maxlen);
            free(start);

            // Append the actual content of the row
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols - maxlen)
                len = E.screencols - maxlen;
            char *c = &E.row[filerow].render[E.coloff];       // Rendered content
            unsigned char *hl = &E.row[filerow].hl[E.coloff]; // Syntax highlighting values
            int cc = -1;                                      // Current color
            int j;
            for (j = 0; j < len; j++)
            {
                if (iscntrl(c[j]))
                {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if (cc != -1)
                    {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", cc);
                        abAppend(ab, buf, clen);
                    }
                }
                else
                {
                    // Check if this position is selected
                    int selected = isPositionSelected(filerow, E.coloff + j);
                    int color;

                    if (selected)
                    {
                        color = syntaxToColor(HL_SELECTION);
                    }
                    else if (hl[j] == HL_NORMAL)
                    {
                        color = -1; // No color for normal text
                    }
                    else
                    {
                        color = syntaxToColor(hl[j]);
                    }

                    if (color != cc)
                    {
                        if (cc != -1)
                        {
                            abAppend(ab, "\x1b[39m", 5); // Reset foreground
                            if (cc == syntaxToColor(HL_SELECTION))
                            {
                                abAppend(ab, "\x1b[49m", 5); // Reset background
                            }
                        }
                        cc = color;
                        if (color != -1)
                        {
                            char buf[16];
                            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                            abAppend(ab, buf, clen);
                        }
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5); // Reset foreground color
            abAppend(ab, "\x1b[49m", 5); // Reset background color
        }

        abAppend(ab, "\x1b[K", 3); // End the line
        abAppend(ab, "\r\n", 2);
    }
}

void drawBar(struct abuf *ab)
{
    // Appends a info bar to the buffer
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]", E.numrows,
                       E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                        E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
    if (len > E.screencols)
        len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void drawStatus(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int len = strlen(E.status);
    if (len > E.screencols)
        len = E.screencols;
    if (len && time(NULL) - E.statustime < 5)
        abAppend(ab, E.status, len);
}

void refreshScreen(void)
{
    scroll();

    struct abuf ab = ABUF_INIT; // Initialize the append buffer

    abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
    abAppend(&ab, "\x1b[H", 3);    // Move cursor to top-left corner

    drawRows(&ab);   // Draw the rows
    drawBar(&ab);    // Draw the info bar
    drawStatus(&ab); // Draw status message

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf)); // Move cursor to the current position

    abAppend(&ab, "\x1b[?25h", 6); // Show cursor

    if (write(STDOUT_FILENO, ab.b, ab.len) == -1)
        die("write"); // Write the buffer to stdout
    abFree(&ab);      // Free the buffer
}

void setStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status, sizeof(E.status), fmt, ap);
    va_end(ap);
    E.statustime = time(NULL);
}

/** SYNTAX HIGLIGHTING **/

void renderRowSyntax(erow *row)
{
    // Render the syntax of one row
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL)
        return;

    char **keywords = E.syntax->keywords;

    char *scs = E.syntax->sl_comment_start;
    char *mcs = E.syntax->ml_comment_start;
    char *mce = E.syntax->ml_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->index > 0 && E.row[row->index - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize)
    {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment)
        {
            if (!strncmp(&row->render[i], scs, scs_len))
            {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string)
        {
            if (in_comment)
            {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len))
                {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                else
                {
                    i++;
                    continue;
                }
            }
            else if (!strncmp(&row->render[i], mcs, mcs_len))
            {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS)
        {
            if (in_string)
            {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize)
                {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string)
                    in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            }
            else
            {
                if (c == '"' || c == '\'')
                {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS)
        {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) // Highlight numbers
            {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep)
        {
            int j;
            for (j = 0; keywords[j]; j++)
            {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2)
                    klen--;
                if (!strncmp(&row->render[i], keywords[j], klen) &&
                    is_separator(row->render[i + klen]))
                {
                    memset(&row->hl[i], kw2 ? HL_KEY1 : HL_KEY2, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL)
            {
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep && (isalpha(c) || c == '_'))
        {
            int len = 0;

            while (i + len < row->rsize &&
                   (isalnum(row->render[i + len]) || row->render[i + len] == '_'))
            {
                len++;
            }

            int next_pos = i + len;
            while (next_pos < row->rsize && isspace(row->render[next_pos]))
            {
                next_pos++;
            }

            int is_keyword = 0;
            for (int k = 0; keywords[k]; k++)
            {
                int klen_check = strlen(keywords[k]);
                int kw2_check = keywords[k][klen_check - 1] == '|';
                if (kw2_check)
                    klen_check--;

                if (len == klen_check && !strncmp(&row->render[i], keywords[k], klen_check))
                {
                    is_keyword = 1;
                    break;
                }
            }

            if (!is_keyword)
            {
                int is_function = 0;
                if (next_pos < row->rsize && row->render[next_pos] == '(')
                {
                    int is_macro = 0;

                    int search_pos = 0;
                    while (search_pos < i)
                    {
                        if (row->render[search_pos] == '#')
                        {
                            int def_pos = search_pos + 1;
                            while (def_pos < i && isspace(row->render[def_pos]))
                                def_pos++;

                            if (def_pos + 6 <= i && !strncmp(&row->render[def_pos], "define", 6))
                            {
                                is_macro = 1;
                                break;
                            }
                        }
                        search_pos++;
                    }

                    if (!is_macro)
                    {
                        is_function = 1;
                    }
                }

                if (is_function)
                {
                    memset(&row->hl[i], HL_FUNC, len);
                    i += len;
                    prev_sep = 0;
                    continue;
                }

                int is_var = 0;

                if (i > 0)
                {
                    int prev_end = i - 1;
                    while (prev_end >= 0 && isspace(row->render[prev_end]))
                        prev_end--;

                    if (prev_end >= 0)
                    {
                        int prev_start = prev_end;
                        while (prev_start > 0 &&
                               (isalnum(row->render[prev_start - 1]) || row->render[prev_start - 1] == '_'))
                        {
                            prev_start--;
                        }

                        for (int k = 0; keywords[k]; k++)
                        {
                            int klen_check = strlen(keywords[k]);
                            if (keywords[k][klen_check - 1] == '|')
                            {
                                klen_check--;
                                if ((prev_end - prev_start + 1) == klen_check &&
                                    !strncmp(&row->render[prev_start], keywords[k], klen_check))
                                {
                                    is_var = 1;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!is_var && next_pos < row->rsize)
                {
                    char next_char = row->render[next_pos];
                    if (next_char == '=' || next_char == '[' || next_char == '.' ||
                        next_char == '+' || next_char == '-' || next_char == '*' ||
                        next_char == '/' || next_char == '%' || next_char == '<' ||
                        next_char == '>' || next_char == '!' || next_char == '&' ||
                        next_char == '|' || next_char == '^' || next_char == ',' ||
                        next_char == ';' || next_char == ')' || next_char == ']')
                    {
                        is_var = 1;
                    }
                }

                if (!is_var && i > 0)
                {
                    int prev_pos = i - 1;
                    while (prev_pos >= 0 && isspace(row->render[prev_pos]))
                        prev_pos--;

                    if (prev_pos >= 0)
                    {
                        char prev_char = row->render[prev_pos];
                        if (prev_char == '(' || prev_char == ',' || prev_char == '=' ||
                            prev_char == '+' || prev_char == '-' || prev_char == '*' ||
                            prev_char == '/' || prev_char == '%' || prev_char == '<' ||
                            prev_char == '>' || prev_char == '!' || prev_char == '&' ||
                            prev_char == '|' || prev_char == '^' || prev_char == '[' ||
                            prev_char == '{' || prev_char == ';')
                        {
                            is_var = 1;
                        }
                    }
                }

                if (!is_var)
                {
                    int all_upper = 1;
                    for (int check = 0; check < len; check++)
                    {
                        if (islower(row->render[i + check]))
                        {
                            all_upper = 0;
                            break;
                        }
                    }

                    if (all_upper || (next_pos >= row->rsize || isspace(row->render[next_pos]) ||
                                      row->render[next_pos] == ';' || row->render[next_pos] == ',' ||
                                      row->render[next_pos] == ')' || row->render[next_pos] == ']' ||
                                      row->render[next_pos] == '}'))
                    {
                        is_var = 1;
                    }
                }

                if (is_var)
                {
                    memset(&row->hl[i], HL_VAR, len);
                    i += len;
                    prev_sep = 0;
                    continue;
                }
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->index + 1 < E.numrows)
        renderRowSyntax(&E.row[row->index + 1]);
}

int syntaxToColor(int hl)
{
    // HL color assignments
    switch (hl)
    {
    case HL_COMMENT:
    case HL_MLCOMMENT:
        return 32;
    case HL_KEY1:
        return 34;
    case HL_KEY2:
        return 35;
    case HL_STRING:
        return 91;
    case HL_NUMBER:
        return 92;
    case HL_FUNC:
        return 33;
    case HL_VAR:
        return 96;
    case HL_MATCH:
        return 93;
    case HL_SELECTION:
        return 47; // White background for selection
    default:
        return 37;
    }
}

void renderSyntax(void)
{
    // Render the whole file's syntax
    int filerow;
    for (filerow = 0; filerow < E.numrows; filerow++)
    {
        renderRowSyntax(&E.row[filerow]);
    }
}

void selectSyntax(void)
{
    // Select syntax based on filename
    E.syntax = NULL;
    if (E.filename == NULL)
        return;
    char *ext = strrchr(E.filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
    {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i])
        {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(E.filename, s->filematch[i])))
            {
                E.syntax = s;
                renderSyntax();
                return;
            }
            i++;
        }
    }
}

/* SELECTION */

void clearSelection(void)
{
    // Clear current selection
    E.sel_active = 0;
    E.sel_start_cx = E.sel_start_cy = 0;
    E.sel_end_cx = E.sel_end_cy = 0;
}

void startSelection(void)
{
    // Start a new selection at current cursor position
    int offset = log10(E.numrows) + 2;
    E.sel_active = 1;
    E.sel_start_cx = E.cx - offset;
    E.sel_start_cy = E.cy;
    E.sel_end_cx = E.cx - offset;
    E.sel_end_cy = E.cy;
}

void updateSelection(void)
{
    // Update selection end to current cursor position
    if (E.sel_active)
    {
        int offset = log10(E.numrows) + 2;
        E.sel_end_cx = E.cx - offset;
        E.sel_end_cy = E.cy;
    }
}

int isPositionSelected(int row, int col)
{
    // Check if a position is within the current selection
    if (!E.sel_active)
        return 0;

    int start_row = E.sel_start_cy;
    int start_col = E.sel_start_cx;
    int end_row = E.sel_end_cy;
    int end_col = E.sel_end_cx;

    // Normalize selection so start is always before end
    if (start_row > end_row || (start_row == end_row && start_col > end_col))
    {
        int temp_row = start_row;
        int temp_col = start_col;
        start_row = end_row;
        start_col = end_col;
        end_row = temp_row;
        end_col = temp_col;
    }

    // Check if position is within selection bounds
    if (row < start_row || row > end_row)
        return 0;
    if (row == start_row && col < start_col)
        return 0;
    if (row == end_row && col >= end_col)
        return 0;

    return 1;
}

void deleteSelection(void)
{
    // Delete all text within the current selection
    if (!E.sel_active)
        return;

    int start_row = E.sel_start_cy;
    int start_col = E.sel_start_cx;
    int end_row = E.sel_end_cy;
    int end_col = E.sel_end_cx;

    // Normalize selection so start is always before end
    if (start_row > end_row || (start_row == end_row && start_col > end_col))
    {
        int temp_row = start_row;
        int temp_col = start_col;
        start_row = end_row;
        start_col = end_col;
        end_row = temp_row;
        end_col = temp_col;
    }

    // Position cursor at selection start
    int offset = log10(E.numrows) + 2;
    E.cy = start_row;
    E.cx = start_col + offset;

    if (start_row == end_row)
    {
        // Selection is within a single row
        erow *row = &E.row[start_row];
        memmove(&row->chars[start_col], &row->chars[end_col],
                row->size - end_col + 1);
        row->size -= (end_col - start_col);
        renderRow(row);
        E.dirty++;
    }
    else
    {
        // Selection spans multiple rows

        // First, modify the start row to remove everything from start_col to end
        erow *start_row_ptr = &E.row[start_row];
        start_row_ptr->size = start_col;
        start_row_ptr->chars[start_col] = '\0';

        // If end row exists, append the remainder of end row to start row
        if (end_row < E.numrows)
        {
            erow *end_row_ptr = &E.row[end_row];
            char *remaining = &end_row_ptr->chars[end_col];
            int remaining_len = end_row_ptr->size - end_col;

            start_row_ptr->chars = realloc(start_row_ptr->chars,
                                           start_row_ptr->size + remaining_len + 1);
            memcpy(&start_row_ptr->chars[start_row_ptr->size], remaining, remaining_len);
            start_row_ptr->size += remaining_len;
            start_row_ptr->chars[start_row_ptr->size] = '\0';
        }

        renderRow(start_row_ptr);

        // Delete all rows between start_row+1 and end_row (inclusive)
        for (int i = end_row; i > start_row; i--)
        {
            deleteRow(i);
        }

        E.dirty++;
    }

    clearSelection();
}

void copySelection(void)
{
    // Copy selected text to clipboard using pbcopy
    if (!E.sel_active)
        return;

    int start_row = E.sel_start_cy;
    int start_col = E.sel_start_cx;
    int end_row = E.sel_end_cy;
    int end_col = E.sel_end_cx;

    // Normalize selection so start is always before end
    if (start_row > end_row || (start_row == end_row && start_col > end_col))
    {
        int temp_row = start_row;
        int temp_col = start_col;
        start_row = end_row;
        start_col = end_col;
        end_row = temp_row;
        end_col = temp_col;
    }

    // Create a pipe to pbcopy
    FILE *pbcopy = popen("pbcopy", "w");
    if (!pbcopy)
    {
        setStatusMessage("Error: Could not access clipboard");
        return;
    }

    if (start_row == end_row)
    {
        // Selection is within a single row
        erow *row = &E.row[start_row];
        for (int i = start_col; i < end_col && i < row->size; i++)
        {
            fputc(row->chars[i], pbcopy);
        }
    }
    else
    {
        // Selection spans multiple rows

        // First row: from start_col to end of line
        erow *row = &E.row[start_row];
        for (int i = start_col; i < row->size; i++)
        {
            fputc(row->chars[i], pbcopy);
        }
        fputc('\n', pbcopy);

        // Middle rows: entire rows
        for (int r = start_row + 1; r < end_row; r++)
        {
            row = &E.row[r];
            for (int i = 0; i < row->size; i++)
            {
                fputc(row->chars[i], pbcopy);
            }
            fputc('\n', pbcopy);
        }

        // Last row: from beginning to end_col
        if (end_row < E.numrows)
        {
            row = &E.row[end_row];
            for (int i = 0; i < end_col && i < row->size; i++)
            {
                fputc(row->chars[i], pbcopy);
            }
        }
    }

    pclose(pbcopy);
    setStatusMessage("Text copied to clipboard");
}

void pasteFromClipboard(void)
{
    // Delete selected text if any
    if (E.sel_active)
    {
        deleteSelection();
    }

    // Get text from clipboard using pbpaste
    FILE *pbpaste = popen("pbpaste", "r");
    if (!pbpaste)
    {
        setStatusMessage("Error: Could not access clipboard");
        return;
    }

    // Read clipboard content directly and insert character by character
    int c;
    int char_count = 0;
    while ((c = fgetc(pbpaste)) != EOF)
    {
        if (c == '\n')
        {
            insertNewline();
        }
        else if (c != '\r') // Skip carriage returns
        {
            insertChar(c);
        }
        char_count++;

        // Prevent extremely long pastes from freezing the editor
        if (char_count > 100000)
        {
            setStatusMessage("Paste truncated - too large");
            break;
        }
    }

    pclose(pbpaste);

    if (char_count == 0)
    {
        setStatusMessage("Clipboard is empty");
    }
    else
    {
        setStatusMessage("Text pasted from clipboard");
    }
}
