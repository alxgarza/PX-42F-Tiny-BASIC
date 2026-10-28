#ifndef BIO_H
#define BIO_H

#include <stdbool.h>
#include <stdint.h>

/* What the interpreter (basic.c) needs from the machine it runs on: the
 * console, the keys, the screen, the speaker, the files. main.c and
 * console.c give it the PX-42F's; tests/test_basic.c a stand-in, so the
 * language can be tested on a computer. */

/* Text to the console: '\n' starts a new line. */
void bio_out(const char *s, int n);
/* The cursor's column, 0 = the left edge (PRINT's ',' tabs from it). */
int bio_column(void);
/* A line typed for INPUT, without its end: its length, or -1 if the
 * program is to stop (R/S, or the app is ending). */
int bio_input(char *buf, int size);
/* Called between statements: true if the program is to stop. */
bool bio_stop(void);

/* KEY(): the next key pressed while the program runs, as row and column
 * from 1 (11 = the top-left key ... 75 = +), or 0 if none. */
int bio_key(void);

void bio_cls(void);
void bio_at(int col, int row);
void bio_pset(int x, int y, int black);
void bio_line(int x0, int y0, int x1, int y1, int black);
void bio_box(int x, int y, int w, int h, int black);
void bio_tone(int tone);
/* Waits ms: false if the program is to stop instead. */
bool bio_wait(int ms);
uint32_t bio_random(void);

/* Program files, by name without ".BAS". */
int bio_open(const char *name, bool write); /* a number, or -1 */
int bio_read(int file, char *buf, int n);   /* bytes read, 0 at the end */
int bio_write(int file, const char *buf, int n);
bool bio_close(int file);                   /* false if what was written is lost */
bool bio_remove(const char *name);
#define BIO_NAME_MAX 20
int bio_files(char (*names)[BIO_NAME_MAX + 1], int max); /* sorted */

#endif
