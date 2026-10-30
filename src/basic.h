#ifndef BASIC_H
#define BASIC_H

#include <stdbool.h>

/* PX-BASIC, in the style of Palo Alto Tiny BASIC, with the calculator's
 * own numbers: 34-digit decimals (px->dec*, Plus42's decimal library),
 * variables A-Z, one array @(), line numbers 1-32767. The machine it runs
 * on is bio.h's. */

#define BASIC_LINE_MAX  80    /* characters in a line typed or stored */
#define BASIC_PROG_MAX  16384 /* bytes of program */
#define BASIC_ARRAY_MAX 1024  /* @(0) .. @(1023) */

/* One line as typed at the prompt: with a line number, it goes into the
 * program (or, with nothing after the number, that line goes); without,
 * it runs at once. Its output, and any error, go to bio_out(). */
void basic_enter(const char *line);

/* The program and the variables gone. */
void basic_new(void);

/* The program's lines, for an editor. n is a line number, or 0 for "no
 * line yet": the number of the nearest line before n (0 = the last line) or
 * after it (0 = the first); 0 if there is none. */
int basic_line_before(int n);
int basic_line_after(int n);
/* "100 PRINT X", as LIST shows it: line n, or false if there isn't one.
 * buf needs BASIC_LINE_MAX + 8. */
bool basic_line_get(int n, char *buf, int size);

#define BASIC_VERSION "1.2"

/* Bytes free for the program. */
int basic_free(void);

/* The program differs from what basic_saved() last said was in AUTOSAVE.BAS:
 * any edit, NEW or LOAD since. SAVE and LOAD of a named program don't count
 * as saving it there -- AUTOSAVE is the program as it is when BASIC is left,
 * whatever else was saved or loaded. */
bool basic_changed(void);
void basic_saved(void);

#endif
