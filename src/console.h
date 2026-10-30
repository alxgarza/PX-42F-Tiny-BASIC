#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

/* The PX-42F as a BASIC terminal: lines of text under a status bar on the
 * screen, typed on its keys -- or on a computer's, over the USB console,
 * which also gets everything shown. */

#define CONSOLE_COLS 31
#define CONSOLE_ROWS 9

void console_init(void);

/* Text at the cursor: '\n' starts a line; past the last column it wraps,
 * past the last line the text scrolls up. Also to the USB console. */
void console_out(const char *s, int n);
int console_column(void);
void console_cls(void);
void console_at(int col, int row);

/* Something was drawn on the screen (graphics): it goes out with the next
 * console_flush(). */
void console_drawn(void);
/* The screen to the panel, if anything changed. */
void console_flush(void);

/* A line typed, without its end: its length; or -1 -- the app is to end
 * (ON, confirmed; the Sleep Timer), or, for INPUT in a running program
 * (in_program), R/S or ON. The keys start in ALPHA -- in NUM for INPUT. */
int console_input(char *buf, int size, bool in_program);

/* A program runs: the status bar says so. */
void console_running(bool running);
/* Called between statements: true if the program is to stop (R/S or ON,
 * Ctrl-C on the USB console, the Sleep Timer). Keeps the screen and the watchdog going, and
 * keeps the keys pressed for KEY(). */
bool console_poll(void);
/* Waits ms, as console_poll() does meanwhile: false if stopped. */
bool console_wait(int ms);
/* KEY(): the next key pressed while running, 11 (top left) to 75 (+), or
 * 0. */
int console_key(void);
/* INKEY$: the next key as the character it types, or 0. */
int console_inkey(void);

/* HELP: the keyword map (SHIFT down) to read: the up and down arrows turn
 * its pages, any word's key or ON closes it, and nothing is typed. */
void console_help(void);

/* EXIT: as if ON had been confirmed. */
void console_exit(void);

/* ON was pressed (or the Sleep Timer ran out): the app is to end. */
bool console_quit(void);

#endif
