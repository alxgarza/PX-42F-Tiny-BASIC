/* The language, tested on a computer (make test): lines go in as typed at
 * the prompt (basic_enter()), and what the console shows is compared with
 * what it should. The machine is a stand-in (below): the console is a
 * buffer, the screen and sound a log, files in memory. The numbers are the
 * simulator's (sim/sim_dec.c): doubles, so the expected results are ones
 * that print the same at 12 digits as the calculator's decimals. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "basic.h"
#include "bio.h"
#include "px.h"
#include "sim_dec.h"

static int failures;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

/* ===================== the stand-in machine ===================== */

static const px_api_t api = {
        .dec = sim_dec_op,
        .dec_from_int = sim_dec_from_int,
        .dec_to_int = sim_dec_to_int,
        .dec_from_string = sim_dec_from_string,
        .dec_to_string = sim_dec_to_string,
        .dec_cmp = sim_dec_cmp,
        .dec_class = sim_dec_class,
};
const px_api_t *px = &api;

static char output[8192], machine[1024];
static int output_len, column;
static const char *inputs[8];
static int input_count, input_next;
static int stop_after = -1, stop_calls;

static void log_machine(const char *fmt, int a, int b, int c, int d, int e) {
    size_t n = strlen(machine);
    snprintf(machine + n, sizeof(machine) - n, fmt, a, b, c, d, e);
}

void bio_out(const char *s, int n) {
    for (int i = 0; i < n && output_len < (int) sizeof(output) - 1; i++) {
        output[output_len++] = s[i];
        column = s[i] == '\n' ? 0 : column + 1;
    }
    output[output_len] = 0;
}

int bio_column(void) {
    return column;
}

int bio_input(char *buf, int size) {
    if (input_next >= input_count)
        return -1;
    snprintf(buf, (size_t) size, "%s", inputs[input_next++]);
    bio_out(buf, (int) strlen(buf));
    bio_out("\n", 1);
    return (int) strlen(buf);
}

bool bio_stop(void) {
    return stop_after >= 0 && ++stop_calls > stop_after;
}

int bio_key(void) {
    return 0;
}

void bio_cls(void) {
    log_machine("CLS ", 0, 0, 0, 0, 0);
}

void bio_at(int col, int row) {
    log_machine("AT %d %d ", col, row, 0, 0, 0);
}

void bio_pset(int x, int y, int black) {
    log_machine("PSET %d %d %d ", x, y, black, 0, 0);
}

void bio_line(int x0, int y0, int x1, int y1, int black) {
    log_machine("LINE %d %d %d %d %d ", x0, y0, x1, y1, black);
}

void bio_box(int x, int y, int w, int h, int black) {
    log_machine("BOX %d %d %d %d %d ", x, y, w, h, black);
}

void bio_tone(int tone) {
    log_machine("TONE %d ", tone, 0, 0, 0, 0);
}

bool bio_wait(int ms) {
    log_machine("WAIT %d ", ms, 0, 0, 0, 0);
    return true;
}

uint32_t bio_random(void) {
    static uint32_t x = 1;
    x = x * 1664525u + 1013904223u;
    return x;
}

/* Files: a few, in memory. */
static struct {
    char name[BIO_NAME_MAX + 1];
    char data[4096];
    int len;
} disk[4];
typedef struct {
    int file, pos; /* file: disk[] + 1, 0 = not open */
    bool write;
} handle_t;
static handle_t open_files[2];

int bio_open(const char *name, bool write) {
    int f = -1;
    for (int i = 0; i < 4 && f < 0; i++)
        if (strcmp(disk[i].name, name) == 0)
            f = i;
    if (f < 0 && write)
        for (int i = 0; i < 4 && f < 0; i++)
            if (disk[i].name[0] == 0) {
                f = i;
                snprintf(disk[i].name, sizeof(disk[i].name), "%s", name);
            }
    if (f < 0)
        return -1;
    if (write)
        disk[f].len = 0;
    for (int h = 0; h < 2; h++)
        if (!open_files[h].write && open_files[h].file == 0) {
            open_files[h] = (handle_t) {f + 1, 0, write};
            return h;
        }
    return -1;
}

int bio_read(int h, char *buf, int n) {
    int f = open_files[h].file - 1;
    int left = disk[f].len - open_files[h].pos;
    n = n < left ? n : left;
    memcpy(buf, disk[f].data + open_files[h].pos, (size_t) n);
    open_files[h].pos += n;
    return n;
}

int bio_write(int h, const char *buf, int n) {
    int f = open_files[h].file - 1;
    if (disk[f].len + n > (int) sizeof(disk[f].data))
        return -1;
    memcpy(disk[f].data + disk[f].len, buf, (size_t) n);
    disk[f].len += n;
    return n;
}

bool bio_close(int h) {
    open_files[h].file = 0;
    open_files[h].write = false;
    return true;
}

bool bio_remove(const char *name) {
    for (int i = 0; i < 4; i++)
        if (disk[i].name[0] != 0 && strcmp(disk[i].name, name) == 0) {
            disk[i].name[0] = 0;
            return true;
        }
    return false;
}

int bio_files(char (*names)[BIO_NAME_MAX + 1], int max) {
    int n = 0;
    for (int i = 0; i < 4 && n < max; i++)
        if (disk[i].name[0] != 0)
            snprintf(names[n++], BIO_NAME_MAX + 1, "%s", disk[i].name);
    return n;
}

/* ===================== the tests ===================== */

/* Lines typed, one after the other, into a new program: what the console
 * showed. */
static const char *typed(const char *const *lines, int count) {
    output_len = 0;
    output[0] = 0;
    column = 0;
    machine[0] = 0;
    for (int i = 0; i < count; i++)
        basic_enter(lines[i]);
    return output;
}

#define TYPED(...) typed((const char *const[]) {__VA_ARGS__}, sizeof((const char *const[]) {__VA_ARGS__}) / sizeof(char *))

static void expect(const char *got, const char *want, const char *what) {
    CHECK(strcmp(got, want) == 0, "%s: got \"%s\", wanted \"%s\"", what, got, want);
}

static void test_arithmetic(void) {
    basic_new();
    expect(TYPED("PRINT 2+3*4"), "14\n", "precedence");
    expect(TYPED("PRINT 0.1+0.2"), "0.3\n", "decimals");
    expect(TYPED("PRINT 10/4"), "2.5\n", "division");
    expect(TYPED("PRINT 7 MOD 3, -7 MOD 3"), "1       2\n", "MOD, and a comma's tab");
    expect(TYPED("PRINT 2^10;\" \";2^3^2"), "1024 512\n", "powers, right to left");
    expect(TYPED("PRINT -2^2"), "-4\n", "minus before a power");
    expect(TYPED("PRINT 1/3"), "0.333333333333\n", "12 digits");
    expect(TYPED("DIGITS 5:PRINT PI:DIGITS 12"), "3.1416\n", "DIGITS");
    expect(TYPED("PRINT 1E20;\" \";1.5E-7;\" \";0.000123"), "1E20 1.5E-7 0.000123\n", "exponents");
    expect(TYPED("PRINT 123456789012;\" \";1234567890123"), "123456789012 1.23456789012E12\n", "12 digits or E");
    expect(TYPED("PRINT 0.9999999999999"), "1\n", "rounding up a 9 carries");
    expect(TYPED("PRINT SQR(16);INT(-2.5);ABS(-3);SGN(-9)"), "4-33-1\n", "functions");
    expect(TYPED("DEG:PRINT SIN(30);\" \";ATN(1):RAD"), "0.5 45\n", "degrees");
    expect(TYPED("PRINT 3>2 AND 2>3;NOT 0;1=1 OR 0"), "011\n", "logic");
    expect(TYPED("E=2:PRINT 3*E;\" \";2E3"), "6 2000\n", "E as a variable, E in a number");
    expect(TYPED("? 5"), "5\n", "? is PRINT");
    expect(TYPED("@(5)=7:PRINT @(5)+1"), "8\n", "the array");
    expect(TYPED("X=RND(6):PRINT X>=1 AND X<=6 AND X=INT(X)"), "1\n", "RND(6)");
}

static void test_errors(void) {
    basic_new();
    expect(TYPED("PRINT 1/0"), "HOW?\n", "division by zero");
    expect(TYPED("PRINT SQR(-1)"), "HOW?\n", "no answer");
    expect(TYPED("PRIMT 1"), "WHAT?\n", "a word it doesn't know");
    expect(TYPED("PRINT (1"), "WHAT?\n", "a missing )");
    expect(TYPED("RETURN"), "HOW?\n", "RETURN with no GOSUB");
    expect(TYPED("@(2000)=1"), "HOW?\n", "the array's end");
    expect(TYPED("10 GOTO 99", "RUN"), "HOW? IN 10\n", "a line that isn't there");
    expect(TYPED("NEW", "10 PRINT 1", "20 PRINT 1+", "RUN"), "1\nWHAT? IN 20\n", "an error in a program");
}

static void test_programs(void) {
    expect(TYPED("NEW", "10 S=0", "20 FOR I=1 TO 10", "30 S=S+I", "40 NEXT I", "50 PRINT S", "RUN"), "55\n",
           "FOR / NEXT");
    expect(TYPED("NEW", "10 FOR I=3 TO 1 STEP -1:PRINT I;:NEXT", "20 PRINT", "RUN"), "321\n", "a negative STEP");
    expect(TYPED("NEW", "10 FOR I=0 TO 1 STEP 0.25:PRINT I;\" \";:NEXT", "RUN"), "0 0.25 0.5 0.75 1 ",
           "a fraction STEP");
    expect(TYPED("NEW", "10 GOSUB 100", "20 PRINT \"B\"", "30 END", "100 PRINT \"A\";", "110 RETURN", "RUN"), "AB\n",
           "GOSUB / RETURN");
    expect(TYPED("NEW", "10 IF 1 THEN PRINT \"Y\"", "20 IF 0 THEN PRINT \"N\":PRINT \"N\"", "30 IF 1 THEN 50",
                 "40 PRINT \"X\"", "50 PRINT \"Z\"", "RUN"),
           "Y\nZ\n", "IF");
    expect(TYPED("NEW", "20 B", "10 A", "30 C", "20", "LIST"), "10 A\n30 C\n", "lines in order, one taken out");
    expect(TYPED("NEW", "10 print \"Hi\";x", "LIST"), "10 PRINT \"Hi\";X\n", "upper case but in quotes");
    expect(TYPED("NEW", "10 PRINT 1", "20 PRINT 2", "30 PRINT 3", "LIST 20"), "20 PRINT 2\n30 PRINT 3\n", "LIST from");
    expect(TYPED("LIST 10,20"), "10 PRINT 1\n20 PRINT 2\n", "LIST a range");
}

/* What the editor walks through: the lines before and after a number. */
static void test_editor_lines(void) {
    TYPED("NEW", "30 PRINT 3", "10 PRINT 1", "20 A=1:B=2");
    CHECK(basic_line_before(0) == 30, "the last line is 30, not %d", basic_line_before(0));
    CHECK(basic_line_before(30) == 20 && basic_line_before(20) == 10, "lines before");
    CHECK(basic_line_before(10) == 0, "nothing before the first line");
    CHECK(basic_line_after(0) == 10, "the first line is 10, not %d", basic_line_after(0));
    CHECK(basic_line_after(10) == 20 && basic_line_after(25) == 30, "lines after, with a gap");
    CHECK(basic_line_after(30) == 0, "nothing after the last line");
    char text[BASIC_LINE_MAX + 8];
    CHECK(basic_line_get(20, text, sizeof(text)) && strcmp(text, "20 A=1:B=2") == 0, "line 20 as LIST shows it: \"%s\"", text);
    CHECK(!basic_line_get(25, text, sizeof(text)), "no line 25");
    TYPED("NEW");
    CHECK(basic_line_before(0) == 0 && basic_line_after(0) == 0, "an empty program has no lines");
}

static void test_input(void) {
    inputs[0] = "abc";
    inputs[1] = " 4.5 ";
    input_count = 2;
    input_next = 0;
    expect(TYPED("NEW", "10 INPUT N", "20 PRINT N*2", "RUN"), "N? abc\nHOW?\nN?  4.5 \n9\n", "INPUT asks again");
    inputs[0] = "3";
    input_count = 1;
    input_next = 0;
    expect(TYPED("NEW", "10 INPUT \"HOW MANY\";@(1)", "20 PRINT @(1)", "RUN"), "HOW MANY3\n3\n", "INPUT's prompt");
    input_count = 0;
    input_next = 0;
    expect(TYPED("NEW", "10 INPUT N", "RUN"), "N? \nBREAK IN 10\n", "R/S in INPUT");
}

static void test_files(void) {
    TYPED("NEW", "10 PRINT \"A\"", "20 FOR I=1 TO 2:PRINT I:NEXT", "SAVE \"T\"");
    CHECK(!basic_changed(), "a program just saved is unchanged");
    TYPED("NEW");
    CHECK(basic_changed(), "NEW is a change");
    expect(TYPED("LOAD \"T\"", "LIST"), "10 PRINT \"A\"\n20 FOR I=1 TO 2:PRINT I:NEXT\n", "SAVE and LOAD");
    CHECK(!basic_changed(), "a program just loaded is unchanged");
    expect(TYPED("FILES"), "T         \n", "FILES");
    expect(TYPED("ERASE \"T\"", "FILES"), "", "ERASE");
    expect(TYPED("LOAD \"T\""), "HOW?\n", "LOAD what isn't there");
    expect(TYPED("SAVE \"A/B\""), "HOW?\n", "a name with a /");
    expect(TYPED("NEW", "10 PRINT 7", "save \"Ring_3d\"", "NEW", "load \"RING_3D\"", "RUN"), "7\n",
           "a name in lower case is the same name in capitals");
    expect(TYPED("FILES"), "RING_3D   \n", "listed in capitals");
    expect(TYPED("NEW", "load ring_3d", "LIST"), "10 PRINT 7\n", "a name without quotes");
    expect(TYPED("LOAD RING 3D"), "WHAT?\n", "a name without quotes ends at a space");
    TYPED("ERASE \"ring_3d\"");
}

static void test_machine(void) {
    basic_new();
    TYPED("CLS:PSET 1,2:LINE 0,0,9,9,0:BOX 1,1,5,5:TONE 3:AT 2,3:WAIT 100");
    CHECK(strcmp(machine, "CLS PSET 1 2 1 LINE 0 0 9 9 0 BOX 1 1 5 5 1 TONE 3 AT 2 3 WAIT 100 ") == 0, "graphics: %s",
          machine);
    expect(TYPED("TONE 11"), "HOW?\n", "TONE 0-10 only");
}

static void test_limits(void) {
    stop_after = 1000;
    stop_calls = 0;
    expect(TYPED("NEW", "10 GOTO 10", "RUN"), "BREAK IN 10\n", "R/S stops a loop");
    stop_after = -1;

    basic_new();
    char line[BASIC_LINE_MAX + 1];
    const char *result = "";
    for (int n = 1; n < 1000 && result[0] == 0; n++) {
        snprintf(line, sizeof(line), "%d REM %060d", n, n);
        result = TYPED(line);
    }
    expect(result, "SORRY\n", "a full program");
    CHECK(basic_free() < 80, "SORRY with %d bytes free", basic_free());
    basic_new();
}

static void on_alarm(int sig) {
    (void) sig;
    static const char msg[] = "FAIL: a case hung (alarm)\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(10);
    test_arithmetic();
    test_errors();
    test_programs();
    test_editor_lines();
    test_input();
    test_files();
    test_machine();
    test_limits();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
