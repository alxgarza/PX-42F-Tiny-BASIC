/* The screen, the keys and the USB console as one BASIC terminal. */
#include "console.h"

#include <stdio.h>
#include <string.h>

#include "basic.h"
#include "px.h"

/* The screen: a 12-pixel status bar, then CONSOLE_ROWS lines of the small
 * font (8 x 12). */
#define CHAR_W     8
#define CHAR_H     12
#define TEXT_X     1
#define TEXT_Y     13
#define FLUSH_MS   100 /* while running: the screen at most this often */
#define POLL_MS    20  /* while running: the keys and the console this often */

static int col, row;

/* What's been printed on the current line, for the USB console: the prompt
 * the typed text goes after (console_input() redraws it there). */
static char out_line[64];
static int out_line_len;
static bool dirty, running, typing, quit;
static uint32_t last_flush, last_poll;

/* What came in on the USB console while a program ran (but Ctrl-C): typed
 * ahead, for the next line. */
static char ahead[256];
static int ahead_len;

/* The last character from the USB console was a CR: a LF right after it
 * is the same Enter (terminals send CR LF). Kept from one line to the
 * next -- the LF often comes in the next read, after the CR has ended a
 * line: taken alone, it was a second, empty Enter. */
static bool after_cr;

/* KEY()'s queue: keys pressed while a program runs. */
static int key_queue[16];
static int key_head, key_tail;

/* ===================== the keyboard ===================== */

/* What each key types, by place (row 0-6, column 0-5), in each mode. 0:
 * the key does something else -- ENTER, <-, the two arrows, SHIFT, ON.
 * ALPHA: the letters where the calculator's ALPHA keyboard has them. NUM:
 * digits and operators where they're printed, symbols above. */
static const char abc_keys[7][6] = {
        {'A', 'B', 'C', 'D', 'E', 'F'},
        {'G', 'H', 'I', 'J', 'K', 'L'},
        {0, 'M', 'N', 'O', 0, 0},
        {0, 'P', 'Q', 'R', 'S', 0},
        {0, 'T', 'U', 'V', 'W', 0},
        {0, 'X', 'Y', 'Z', '=', 0},
        {0, ':', '.', '?', ' ', 0},
};
static const char num_keys[7][6] = {
        {'(', ')', '"', ',', ';', ':'},
        {'<', '>', '=', '@', '\'', '?'},
        {0, '^', '#', 'E', 0, 0},
        {0, '7', '8', '9', '/', 0},
        {0, '4', '5', '6', '*', 0},
        {0, '1', '2', '3', '-', 0},
        {0, '0', '.', ' ', '+', 0},
};

#define KEY_UP   PX_KEY_ARROW_UP /* a line; in a line, the cursor left */
#define KEY_DOWN PX_KEY_ARROW_DN /* a line; in a line, the cursor right; in the keyword map, the next page */

/* The lines typed at the prompt, for SHIFT up and the terminal's up arrow:
 * the last HISTORY. */
#define HISTORY  8
#define LINE_MAX 80
static char history[HISTORY][LINE_MAX + 1];
static int history_count, history_newest;

/* The keyword map (SHIFT down): two pages, by place. A keyword is
 * typed as it is here; its label is the same without a trailing space or
 * quote. */
static const char *const keyword_pages[2][7][6] = {
        {
                {"PRINT ", "INPUT ", "IF ", "THEN ", "GOTO ", "GOSUB "},
                {"FOR ", "TO ", "STEP ", "NEXT ", "RETURN", "END"},
                {"RUN", "LET ", "REM ", "CLS", "LIST", NULL},
                {"SAVE \"", "LOAD \"", "FILES", "NEW", "ERASE \"", NULL},
                {"AT ", "PSET ", "LINE ", "BOX ", "TONE ", NULL},
                {"WAIT ", "DEG", "RAD", "DIGITS ", "STOP", NULL},
                {NULL, NULL, NULL, NULL, NULL, NULL},
        },
        {
                {"SIN(", "COS(", "TAN(", "ASN(", "ACS(", "ATN("},
                {"SQR(", "EXP(", "LOG(", "LOG10(", "ABS(", "SGN("},
                {"INT(", "RND(", "PI", "KEY()", "SIZE", NULL},
                {"AND ", "OR ", "NOT ", "MOD ", "@(", NULL},
                {NULL, NULL, NULL, NULL, NULL, NULL},
                {NULL, NULL, NULL, NULL, NULL, NULL},
                {NULL, NULL, NULL, NULL, NULL, NULL},
        },
};

static bool number_mode; /* NUM, not ALPHA */
static bool shifted;

/* ===================== the screen ===================== */

static void status_bar(void) {
    px->fill(0, 0, PX_SCREEN_W, 12, 1);
    px->text(2, 0, "BASIC", PX_FONT_SMALL, 1);
    const char *right = running && !typing ? "RUN" : number_mode != shifted ? "NUM" : "ALPHA";
    int x = PX_SCREEN_W - 2 - px->text_width(right, PX_FONT_SMALL);
    px->text(x, 0, right, PX_FONT_SMALL, 1);
    if (shifted && typing)
        px->text(x - 8 - px->text_width("SHIFT", PX_FONT_SMALL), 0, "SHIFT", PX_FONT_SMALL, 1);
    dirty = true;
}

static void draw_cell(int c, int r, char ch, bool inverse) {
    char s[2] = {ch, 0};
    px->text(TEXT_X + c * CHAR_W, TEXT_Y + r * CHAR_H, s, PX_FONT_SMALL, inverse);
    dirty = true;
}

/* Everything below the status bar up a line; a blank line at the bottom. */
static void scroll(void) {
    uint8_t *s = px->screen;
    memmove(s + TEXT_Y * PX_SCREEN_STRIDE, s + (TEXT_Y + CHAR_H) * PX_SCREEN_STRIDE,
            (size_t) (PX_SCREEN_H - TEXT_Y - CHAR_H) * PX_SCREEN_STRIDE);
    px->fill(0, TEXT_Y + (CONSOLE_ROWS - 1) * CHAR_H, PX_SCREEN_W, PX_SCREEN_H - (TEXT_Y + (CONSOLE_ROWS - 1) * CHAR_H),
             0);
    dirty = true;
}

static void new_line(void) {
    col = 0;
    if (++row == CONSOLE_ROWS) {
        row = CONSOLE_ROWS - 1;
        scroll();
    }
}

void console_init(void) {
    px->clear();
    col = row = 0;
    running = quit = false;
    status_bar();
}

void console_out(const char *s, int n) {
    int start = 0; /* the run of text not yet sent to the USB console */
    for (int i = 0; i < n; i++) {
        if (s[i] == '\n') {
            px->serial_write(s + start, i - start);
            px->serial_write("\r\n", 2);
            start = i + 1;
            out_line_len = 0;
            new_line();
            continue;
        }
        if (out_line_len < (int) sizeof(out_line))
            out_line[out_line_len++] = s[i];
        if (col == CONSOLE_COLS)
            new_line();
        draw_cell(col++, row, s[i], false);
    }
    px->serial_write(s + start, n - start);
}

int console_column(void) {
    return col;
}

void console_cls(void) {
    px->clear();
    col = row = 0;
    out_line_len = 0;
    if (!running)
        status_bar();
    dirty = true;
}

void console_at(int c, int r) {
    col = c < 0 ? 0 : c >= CONSOLE_COLS ? CONSOLE_COLS - 1 : c;
    row = r < 0 ? 0 : r >= CONSOLE_ROWS ? CONSOLE_ROWS - 1 : r;
}

void console_drawn(void) {
    dirty = true;
}

void console_flush(void) {
    if (dirty) {
        px->show();
        dirty = false;
        last_flush = px->millis();
    }
}

bool console_quit(void) {
    return quit;
}

void console_running(bool r) {
    running = r;
    status_bar();
    if (!running)
        console_flush();
}

/* ===================== the keyword map ===================== */

/* A key's box on the map, laid out like the keyboard: 6 keys in the top
 * two rows, 5 wider ones in each row below (ENTER's too). */
static void key_box(int r, int c, int *x, int *w) {
    *w = r < 2 ? 41 : 49;
    *x = 1 + c * *w;
}

static void draw_keyword_page(int page) {
    px->clear();
    px->fill(0, 0, PX_SCREEN_W, 12, 1);
    px->text(2, 0, page == 0 ? "STATEMENTS" : "FUNCTIONS", PX_FONT_SMALL, 1);
    const char *hint = page == 0 ? "\x0e:more" : "\x0e:back";
    px->text(PX_SCREEN_W - 2 - px->text_width(hint, PX_FONT_SMALL), 0, hint, PX_FONT_SMALL, 1);
    /* Every key, as the keyboard has them: those with no keyword as empty
     * outlines, ON as BACK (it closes the map, as they do). */
    for (int r = 0; r < 7; r++) {
        int y = 14 + r * 15;
        for (int c = 0; c < (r < 2 ? 6 : 5); c++) {
            const char *k = keyword_pages[page][r][c];
            int x, w;
            key_box(r, c, &x, &w);
            if (r == 6 && c == 0)
                k = "BACK";
            if (k == NULL) {
                px->fill(x, y, w - 2, 13, 1);
                px->fill(x + 1, y + 1, w - 4, 11, 0);
                continue;
            }
            char label[8];
            int n = (int) strlen(k);
            while (n > 0 && (k[n - 1] == ' ' || k[n - 1] == '"'))
                n--;
            if (n > 6)
                n = 6;
            memcpy(label, k, (size_t) n);
            label[n] = 0;
            px->fill(x, y, w - 2, 13, 1);
            px->text(x + (w - 2 - n * 6) / 2, y + 3, label, PX_FONT_MINI, 1);
        }
    }
    px->show();
}

/* The down arrow: the keyword map, a page at a time (the arrow again: the
 * next; after the last, back). The keyword chosen, or NULL: a key with
 * none (ON among them) cancels. */
static const char *choose_keyword(void) {
    static uint8_t saved[PX_SCREEN_H * PX_SCREEN_STRIDE];
    memcpy(saved, px->screen, sizeof(saved));
    const char *chosen = NULL;
    int page = 0;
    draw_keyword_page(page);
    while (true) {
        int k = px->key(-1);
        if (k == PX_KEY_QUIT) {
            quit = true;
            break;
        }
        if (k < 0 || (k & PX_KEY_RELEASED))
            continue;
        if (k == KEY_DOWN) {
            if (++page == 2)
                break;
            draw_keyword_page(page);
            continue;
        }
        chosen = keyword_pages[page][PX_KEY_ROW(k)][PX_KEY_COL(k)];
        break;
    }
    memcpy(px->screen, saved, sizeof(saved));
    dirty = true;
    return chosen;
}

/* ===================== typing a line ===================== */

/* The line being typed: its text, the cursor in it, and where it sits on
 * the screen (it may wrap over several rows, and scroll the screen). */
typedef struct {
    char *buf;
    int size, len, pos; /* pos: the cursor, 0 to len */
    int recalled;       /* lines back SHIFT up has gone; -1: none yet */
    int start_col, start_row; /* where its first character goes */
    int drawn;          /* its length when last drawn: what to blank */
    bool redraw;
    bool quiet;         /* the program editor's line: nothing goes to the USB console */
} line_t;

/* The program line last entered or loaded (0: none): the list of the
 * program opens on it -- a line just typed is the one with the typo. */
static int browse_line;

/* The line on the screen: every character from the start, the cursor (a
 * block) at pos, the rest of the old text blanked. The text cursor ends up
 * after the line, where console_out() carries on. */
static void draw_input(line_t *l, bool cursor_on) {
    int rows = (l->start_col + l->len + 1 + CONSOLE_COLS - 1) / CONSOLE_COLS; /* the cursor's cell too */
    while (l->start_row + rows > CONSOLE_ROWS && l->start_row > 0) {
        scroll();
        l->start_row--;
    }
    int upto = (l->drawn > l->len ? l->drawn : l->len) + 1;
    for (int i = 0; i < upto; i++) {
        int at = l->start_col + i, r = l->start_row + at / CONSOLE_COLS;
        if (r >= CONSOLE_ROWS)
            break;
        draw_cell(at % CONSOLE_COLS, r, i < l->len ? l->buf[i] : ' ', cursor_on && i == l->pos);
    }
    l->drawn = l->len;
    int end = l->start_col + l->len;
    if (end > 0 && end % CONSOLE_COLS == 0) { /* a full row: the wrap is still to come */
        col = CONSOLE_COLS;
        row = l->start_row + end / CONSOLE_COLS - 1;
    } else {
        col = end % CONSOLE_COLS;
        row = l->start_row + end / CONSOLE_COLS;
    }
    if (row >= CONSOLE_ROWS)
        row = CONSOLE_ROWS - 1;
    l->redraw = false;
}

/* The USB console shows the line too. Typing at the end just adds the
 * character; anything else redraws the whole line there (ANSI: carriage
 * return, the prompt, the text, clear to the end, the cursor back). */
static void echo_line(const line_t *l) {
    if (l->quiet)
        return;
    px->serial_write("\r", 1);
    px->serial_write(out_line, out_line_len);
    px->serial_write(l->buf, l->len);
    px->serial_write("\x1b[K", 3);
    if (l->pos < l->len) {
        char back[12];
        px->serial_write(back, snprintf(back, sizeof(back), "\x1b[%dD", l->len - l->pos));
    }
}

static void insert_text(line_t *l, const char *s) {
    bool at_end = l->pos == l->len;
    for (; *s && l->len < l->size - 1; s++) {
        memmove(l->buf + l->pos + 1, l->buf + l->pos, (size_t) (l->len - l->pos));
        l->buf[l->pos++] = *s;
        l->len++;
        if (at_end && !l->quiet)
            px->serial_write(s, 1);
    }
    if (!at_end)
        echo_line(l);
    l->redraw = true;
}

static void delete_before(line_t *l) {
    if (l->pos == 0)
        return;
    bool at_end = l->pos == l->len;
    memmove(l->buf + l->pos - 1, l->buf + l->pos, (size_t) (l->len - l->pos));
    l->pos--;
    l->len--;
    if (!at_end)
        echo_line(l);
    else if (!l->quiet)
        px->serial_write("\b \b", 3);
    l->redraw = true;
}

static void delete_at(line_t *l) { /* the Delete key: the character under the cursor */
    if (l->pos < l->len) {
        l->pos++;
        delete_before(l);
    }
}

static void move_to(line_t *l, int pos) {
    pos = pos < 0 ? 0 : pos > l->len ? l->len : pos;
    if (pos != l->pos) {
        l->pos = pos;
        l->redraw = true;
        echo_line(l);
    }
}

/* The whole line replaced, the cursor at its end. */
static void set_text(line_t *l, const char *s) {
    l->len = (int) strlen(s) < l->size - 1 ? (int) strlen(s) : l->size - 1;
    memcpy(l->buf, s, (size_t) l->len);
    l->pos = l->len;
    l->redraw = true;
    echo_line(l);
}

/* SHIFT up (or the terminal's up arrow): the line typed before, in place
 * of this one; again, the one before that. Down: back toward the newest,
 * and then an empty line. At the prompt only. */
static void recall(line_t *l, bool in_program, int older) {
    if (in_program || history_count == 0)
        return;
    int to = l->recalled + older;
    if (to >= history_count)
        to = 0;
    if (to < 0) {
        l->recalled = -1;
        set_text(l, "");
        return;
    }
    l->recalled = to;
    set_text(l, history[(history_newest - to + HISTORY) % HISTORY]);
}

static void remember(const char *line) {
    if (line[0] == 0 || (history_count > 0 && strcmp(history[history_newest], line) == 0))
        return;
    history_newest = (history_newest + 1) % HISTORY;
    snprintf(history[history_newest], sizeof(history[0]), "%s", line);
    if (history_count < HISTORY)
        history_count++;
}

/* ON at the prompt: "LEAVE BASIC?" in the status bar. ENTER, or ON again,
 * says yes (quit is set); any other key no. */
static void confirm_leave(void) {
    px->fill(0, 0, PX_SCREEN_W, 12, 1);
    px->text(2, 0, "LEAVE BASIC? ENTER=YES", PX_FONT_SMALL, 1);
    px->show();
    while (true) {
        int k = px->key(-1);
        if (k == PX_KEY_QUIT || k == PX_KEY_ENTER || k == PX_KEY_ON) {
            quit = true;
            return;
        }
        if (k >= 0 && !(k & PX_KEY_RELEASED))
            return;
    }
}

static void editor(bool up);

/* What one key does to the line: 1 = ENTER, -1 = stop (in_program: R/S
 * or ON) or leave (ON, confirmed), 0 = go on. */
static int key_typed(line_t *l, int k, bool in_program) {
    if (k == PX_KEY_ENTER && shifted) { /* SHIFT ENTER: ALPHA, as printed below it */
        number_mode = !number_mode;
        shifted = false;
        return 0;
    }
    if (k == PX_KEY_ENTER)
        return 1;
    if (in_program && (k == PX_KEY_RS || k == PX_KEY_ON))
        return -1;
    if (k == PX_KEY_ON) {
        confirm_leave();
        return quit ? -1 : 0;
    }
    if (k == PX_KEY_SHIFT) {
        shifted = !shifted;
    } else if (k == KEY_UP || k == KEY_DOWN) {
        bool up = k == KEY_UP, with_shift = shifted;
        shifted = false;
        if (with_shift) { /* SHIFT up: the history; SHIFT down: the keyword map */
            if (up) {
                recall(l, in_program, +1);
            } else {
                const char *kw = choose_keyword();
                if (kw != NULL)
                    insert_text(l, kw);
            }
        } else if (l->len > 0) { /* a line to edit: the arrows move in it */
            move_to(l, l->pos + (up ? -1 : +1));
        } else if (!in_program) { /* an empty line: the program editor */
            editor(up);
            if (quit)
                return -1;
            l->redraw = true;
        }
    } else if (k == PX_KEY_BACKSPACE) {
        if (shifted) { /* SHIFT <-: the whole line */
            set_text(l, "");
            shifted = false;
        } else {
            delete_before(l);
        }
    } else {
        int r = PX_KEY_ROW(k), c = PX_KEY_COL(k);
        char ch = (number_mode != shifted ? num_keys : abc_keys)[r][c];
        shifted = false;
        if (ch != 0) {
            char s[2] = {ch, 0};
            insert_text(l, s);
        }
    }
    return 0;
}

/* ===================== the program editor ===================== */

/* The program on the screen, a line to a row (cut to the screen's width),
 * the current line highlighted. ENTER edits it where it is: all of its
 * text, in as many rows as it needs, the rest of the program still shown
 * around it, the cursor in it. ENTER accepts the change, ON (EXIT)
 * discards it, and either way it's the list again. ON in the list leaves
 * the editor. */

/* Rows a line being edited takes: its text and the cursor's cell. */
static int rows_of(int len) {
    return (len + 1 + CONSOLE_COLS - 1) / CONSOLE_COLS;
}

/* The first line to show, going on from top, so that sel (taking sel_rows)
 * is on the screen -- and a screenful when the program has it, not rows of
 * nothing after its end. */
static int fit_top(int top, int sel, int sel_rows) {
    if (sel < top)
        top = sel;
    while (top != sel) {
        int above = 0;
        for (int n = top; n != sel; n = basic_line_after(n))
            above++;
        if (above + sel_rows <= CONSOLE_ROWS)
            break;
        top = basic_line_after(top);
    }
    while (basic_line_before(top) != 0) {
        int rows = sel_rows - 1;
        for (int n = top; n != 0; n = basic_line_after(n))
            rows++;
        if (rows >= CONSOLE_ROWS)
            break;
        top = basic_line_before(top);
    }
    return top;
}

static void header(const char *label, const char *hint) {
    px->fill(0, 0, PX_SCREEN_W, 12, 1);
    px->text(2, 0, label, PX_FONT_SMALL, 1);
    px->text(PX_SCREEN_W - 2 - px->text_width(hint, PX_FONT_MINI), 2, hint, PX_FONT_MINI, 1);
}

/* edit: the line being edited, or NULL for the list alone. */
static void draw_editor(int top, int sel, const line_t *edit) {
    px->clear();
    if (edit == NULL)
        header("PROGRAM", "\x0e^ MOVE  ENTER EDIT  EXIT BACK");
    else
        header(number_mode != shifted ? "NUM" : "EDIT", "\x0e^ MOVE  ENTER ACCEPT  EXIT DISCARD");
    int row = 0;
    for (int n = top; n != 0 && row < CONSOLE_ROWS; n = basic_line_after(n)) {
        int y = TEXT_Y + row * CHAR_H;
        if (edit != NULL && n == sel) { /* all of it, wrapped, in inverse; the cursor in plain */
            int rows = rows_of(edit->len);
            px->fill(0, y, PX_SCREEN_W, rows * CHAR_H, 1);
            for (int i = 0; i <= edit->len; i++) {
                char c[2] = {i < edit->len ? edit->buf[i] : ' ', 0};
                px->text(TEXT_X + (i % CONSOLE_COLS) * CHAR_W, y + (i / CONSOLE_COLS) * CHAR_H, c, PX_FONT_SMALL,
                         i != edit->pos);
            }
            row += rows;
            continue;
        }
        char text[BASIC_LINE_MAX + 8];
        if (!basic_line_get(n, text, sizeof(text)))
            break;
        text[CONSOLE_COLS] = 0;
        if (n == sel)
            px->fill(0, y, PX_SCREEN_W, CHAR_H, 1);
        px->text(TEXT_X, y, text, PX_FONT_SMALL, n == sel);
        row++;
    }
    px->show();
}

static bool line_exists(int n) {
    char text[BASIC_LINE_MAX + 8];
    return n != 0 && basic_line_get(n, text, sizeof(text));
}

/* Line *sel edited in place. Afterwards *sel is the line to be on: itself,
 * the new number if it was changed (the old line stays: a copy), a
 * neighbour if it was deleted (its number alone), 0 if that was the last
 * line. False if the app is to end. */
static bool edit_line(int *top, int *sel) {
    char buf[BASIC_LINE_MAX + 1], text[BASIC_LINE_MAX + 8];
    line_t e = {buf, sizeof(buf), 0, 0, -1, 0, 0, 0, false, true};
    if (!basic_line_get(*sel, text, sizeof(text)))
        return true;
    set_text(&e, text);
    number_mode = false;
    shifted = false;
    while (true) {
        *top = fit_top(*top, *sel, rows_of(e.len));
        draw_editor(*top, *sel, &e);
        int k = px->key(-1);
        if (k == PX_KEY_QUIT) {
            quit = true;
            return false;
        }
        if (k < 0 || (k & PX_KEY_RELEASED))
            continue;
        int done = key_typed(&e, k, true); /* true: no program list from here, and ON discards */
        if (quit)
            return false;
        if (done < 0)
            return true; /* discarded */
        if (done > 0)
            break;
    }

    /* Accepted. A line without its number (edited away) would run as a
     * command: not what was meant, so it's dropped like a discard. */
    buf[e.len] = 0;
    int number = 0;
    const char *p = buf;
    while (*p == ' ')
        p++;
    for (; *p >= '0' && *p <= '9' && number < 100000; p++)
        number = number * 10 + (*p - '0');
    if (number == 0 || strcmp(buf, text) == 0)
        return true;
    int keep_col = col, keep_row = row; /* an error message goes to the console screen, not this one */
    basic_enter(buf);
    col = keep_col;
    row = keep_row;
    if (line_exists(number)) {
        *sel = number;
    } else if (!line_exists(*sel)) { /* deleted */
        int near = basic_line_after(*sel);
        *sel = near != 0 ? near : basic_line_before(*sel);
    }
    return true;
}

static void editor(bool up) {
    int sel = line_exists(browse_line) ? browse_line : up ? basic_line_before(0) : basic_line_after(0);
    if (sel == 0)
        return;
    static uint8_t saved[PX_SCREEN_H * PX_SCREEN_STRIDE];
    memcpy(saved, px->screen, sizeof(saved));
    int keep_col = col, keep_row = row;
    int top = sel;
    for (int i = 0; i < CONSOLE_ROWS / 2 && basic_line_before(top) != 0; i++)
        top = basic_line_before(top); /* the line in the middle of the screen */
    bool page = false;
    while (sel != 0) {
        top = fit_top(top, sel, 1);
        draw_editor(top, sel, NULL);
        int k = px->key(-1);
        if (k == PX_KEY_QUIT) {
            quit = true;
            break;
        }
        if (k < 0 || (k & PX_KEY_RELEASED))
            continue;
        if (k == PX_KEY_ON || k == PX_KEY_BACKSPACE)
            break;
        if (k == PX_KEY_SHIFT) {
            page = true;
        } else if (k == KEY_UP || k == KEY_DOWN) {
            for (int i = 0; i < (page ? CONSOLE_ROWS : 1); i++) {
                int next = k == KEY_UP ? basic_line_before(sel) : basic_line_after(sel);
                if (next == 0)
                    break;
                sel = next;
            }
            page = false;
        } else if (k == PX_KEY_ENTER) {
            page = false;
            if (!edit_line(&top, &sel))
                break;
        }
    }
    if (sel != 0)
        browse_line = sel;
    memcpy(px->screen, saved, sizeof(saved));
    col = keep_col;
    row = keep_row;
    dirty = true;
}

/* Escape sequences from the USB console: ESC [ A, or ESC O A (some
 * terminals), with a number first for the ones ending in ~. State is kept
 * from one call to the next -- a sequence can arrive in two reads. */
static enum { ESC_NONE, ESC_START, ESC_CSI, ESC_SS3 } esc;
static int esc_number;

/* What one character from the USB console does: as key_typed(). Ctrl-C
 * stops a running program, and clears the line at the prompt. */
static int serial_typed(line_t *l, char ch, bool in_program) {
    if (esc == ESC_START) {
        esc = ch == '[' ? ESC_CSI : ch == 'O' ? ESC_SS3 : ESC_NONE;
        esc_number = 0;
        if (esc != ESC_NONE)
            return 0; /* anything else after Esc is an ordinary character */
    }
    if (esc != ESC_NONE) {
        if (ch >= '0' && ch <= '9') {
            esc_number = esc_number * 10 + (ch - '0');
            return 0;
        }
        if (ch == ';')
            return 0;
        esc = ESC_NONE;
        switch (ch) {
            case 'A': recall(l, in_program, +1); break;
            case 'B': recall(l, in_program, -1); break;
            case 'C': move_to(l, l->pos + 1); break;
            case 'D': move_to(l, l->pos - 1); break;
            case 'H': move_to(l, 0); break;
            case 'F': move_to(l, l->len); break;
            case '~':
                if (esc_number == 1 || esc_number == 7)
                    move_to(l, 0);
                else if (esc_number == 4 || esc_number == 8)
                    move_to(l, l->len);
                else if (esc_number == 3)
                    delete_at(l);
                break;
        }
        return 0;
    }
    if (ch == '\r' || ch == '\n')
        return 1;
    switch (ch) {
        case 3: /* Ctrl-C */
            if (in_program)
                return -1;
            set_text(l, "");
            break;
        case 27: esc = ESC_START; break;
        case 8: case 127: delete_before(l); break;
        case 1: move_to(l, 0); break;                 /* Ctrl-A */
        case 5: move_to(l, l->len); break;            /* Ctrl-E */
        case 2: move_to(l, l->pos - 1); break;        /* Ctrl-B */
        case 6: move_to(l, l->pos + 1); break;        /* Ctrl-F */
        case 16: recall(l, in_program, +1); break;    /* Ctrl-P */
        case 14: recall(l, in_program, -1); break;    /* Ctrl-N */
        case 21: set_text(l, ""); break;              /* Ctrl-U */
        default:
            if (ch >= ' ' && ch < 127) {
                char s[2] = {ch, 0};
                insert_text(l, s);
            }
    }
    return 0;
}

int console_input(char *buf, int size, bool in_program) {
    if (col == CONSOLE_COLS)
        new_line();
    line_t l = {buf, size, 0, 0, -1, col, row, 0, true, false};
    esc = ESC_NONE;
    number_mode = in_program; /* a line typed at the prompt starts in ALPHA; INPUT wants a number */
    shifted = false;
    typing = true;
    status_bar();
    int done = 0;
    bool more = ahead_len > 0; /* serial text waiting: no sleeping until it's read */
    while (done == 0) {
        if (l.redraw)
            draw_input(&l, true);
        if (!more)
            console_flush();
        /* Asleep until a key; on a computer's USB, looking at the console
         * every 20 ms too -- at once while a paste is coming in. */
        int k = px->key(more ? 0 : px->usb_connected() ? POLL_MS : 1000);
        if (k == PX_KEY_QUIT) {
            quit = true;
            done = -1;
        } else if (k >= 0 && !(k & PX_KEY_RELEASED)) {
            done = key_typed(&l, k, in_program);
            if (quit)
                done = -1;
            status_bar();
        }
        char in[sizeof(ahead)];
        int n = 0;
        if (done == 0 && ahead_len > 0) {
            memcpy(in, ahead, (size_t) ahead_len);
            n = ahead_len;
            ahead_len = 0;
        } else if (done == 0) {
            n = px->serial_read(in, 64);
        }
        more = n > 0;
        int i = 0;
        for (; i < n && done == 0; i++) {
            if (in[i] == '\n' && after_cr) { /* CR LF: one Enter */
                after_cr = false;
                continue;
            }
            after_cr = in[i] == '\r';
            done = serial_typed(&l, in[i], in_program);
        }
        if (i < n) { /* the next lines (a program pasted): for the next time */
            memcpy(ahead, in + i, (size_t) (n - i));
            ahead_len = n - i;
        }
    }
    draw_input(&l, false);
    typing = false;
    status_bar();
    buf[l.len] = 0;
    if (done < 0)
        return -1;
    if (!in_program) {
        remember(buf);
        int n = 0; /* the arrows go on from the line just entered */
        const char *p = buf;
        while (*p == ' ')
            p++;
        for (; *p >= '0' && *p <= '9' && n < 100000; p++)
            n = n * 10 + (*p - '0');
        browse_line = n;
    }
    console_out("\n", 1);
    console_flush();
    return l.len;
}

/* ===================== while a program runs ===================== */

/* The keys since the last look: R/S stops, ON ends the app, others go to
 * KEY(). True if the program is to stop. */
static bool look(int wait_ms) {
    bool stop = false;
    int k;
    while ((k = px->key(wait_ms)) != PX_KEY_NONE) {
        wait_ms = 0;
        if (k == PX_KEY_QUIT) {
            quit = true;
            return true;
        }
        if (k & PX_KEY_RELEASED)
            continue;
        if (k == PX_KEY_RS || k == PX_KEY_ON) {
            stop = true;
        } else if ((key_tail + 1) % 16 != key_head) {
            key_queue[key_tail] = (PX_KEY_ROW(k) + 1) * 10 + PX_KEY_COL(k) + 1;
            key_tail = (key_tail + 1) % 16;
        }
    }
    char in[32];
    int n = px->serial_read(in, sizeof(in));
    for (int i = 0; i < n; i++) {
        if (in[i] == 3)
            stop = true;
        else if (ahead_len < (int) sizeof(ahead)) /* full: what comes after is dropped, Ctrl-C still seen */
            ahead[ahead_len++] = in[i];
    }
    return stop;
}

bool console_poll(void) {
    uint32_t now = px->millis();
    if (now - last_poll < POLL_MS)
        return false;
    last_poll = now;
    if (dirty && now - last_flush >= FLUSH_MS)
        console_flush();
    return look(0);
}

bool console_wait(int ms) {
    console_flush();
    uint32_t until = px->millis() + (uint32_t) ms;
    while (true) {
        int32_t left = (int32_t) (until - px->millis());
        if (left <= 0)
            return true;
        if (look((int) left))
            return false;
    }
}

int console_key(void) {
    if (key_head == key_tail)
        return 0;
    int k = key_queue[key_head];
    key_head = (key_head + 1) % 16;
    return k;
}
