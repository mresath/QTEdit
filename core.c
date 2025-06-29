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

/* DATA */

extern struct editorConfig E;

/* PROTOTYPES */

void setStatusMessage(const char *fmt, ...);

void die(char *s);

void resetScreen(void);

void refreshScreen(void);

char *askPrompt(char *prompt);

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
}

void insertRow(int at, char *s, size_t len)
{
    // Insert row to current text in memory
    if (at < 0 || at > E.numrows)
        return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
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
}

void deleteRow(int at)
{
    // Delete a row from the text in memory
    if (at < 0 || at >= E.numrows)
        return;
    freeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
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
    if (E.cy == E.numrows)
    {
        if (E.numrows == 0) {
            E.cx = 2;
        }
        insertRow(E.numrows, "", 0);
    }
    rowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void insertNewline(void)
{
    // Enter press basically
    int offset = log10(E.numrows) + 2; // Offset for line numbers

    if (E.cx == offset)
    {
        insertRow(E.cy, "", 0);
    }
    else
    {
        erow *row = &E.row[E.cy];
        insertRow(E.cy + 1, &row->chars[E.cx - offset], row->size - E.cx + offset);
        row = &E.row[E.cy];
        row->size = E.cx - offset;
        row->chars[row->size] = '\0';
        renderRow(row);
    }
    E.cy++;
    E.cx = offset;
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
        E.filename = askPrompt("Save as: %s (ESC to cancel)");
        if (E.filename == NULL)
        {
            setStatusMessage("Save aborted");
            return;
        }
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
            if (seq[1] >= '0' && seq[1] <= '9') // Possibly 3 long sequences
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

char *askPrompt(char *prompt)
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
            free(buf);
            return NULL;
        }
        else if (c == '\r')
        { // Enter key
            if (buflen > 0)
            {
                setStatusMessage("");
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

void processKeypress(void)
{
    static int quit_count = QUIT_PROT; // Counter for quitting when dirty

    int c = readKey();

    switch (c)
    {
    case '\r':
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

    case HOME_KEY: // Return to start of line on HOME
        E.cx = 0;
        break;
    case END_KEY: // Skip to end of line on END
        if (E.cy < E.numrows)
        {
            E.cx = E.row[E.cy].size;
        }
        break;

    case BACKSPACE:
    case DELETE_KEY:
    case CTRL_KEY('h'):
        if (c == DELETE_KEY)
            moveCursor(ARROW_RIGHT); // Move right if DELETE is pressed
        deleteChar();
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

    case ARROW_LEFT:  // Move left
    case ARROW_RIGHT: // Move right
    case ARROW_DOWN:  // Move down
    case ARROW_UP:    // Move up
        moveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        insertChar(c); // Insert character
        break;
    }

    quit_count = QUIT_PROT; // Reset quit count after any keypress
}

/** OUTPUT **/

void scroll(void)
{
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
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
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
            int maxlen = log10(E.numrows) + 2;

            char starttext[8];
            int nlen = sprintf(starttext, "%d ", filerow + 1);

            int len = E.row[filerow].rsize - E.coloff + maxlen;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;

            char *text = malloc(len + 1);
            char *tp = text;
            for (int i = 0; i < maxlen; i++)
            {
                if (i < nlen)
                    *tp++ = starttext[i];
                else
                    *tp++ = ' ';
            }
            memcpy(tp, &E.row[filerow].render[E.coloff], len - maxlen);

            abAppend(ab, text, len);
            free(text);
        }

        abAppend(ab, "\x1b[K", 3);
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
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                        E.cy + 1, E.numrows);
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
