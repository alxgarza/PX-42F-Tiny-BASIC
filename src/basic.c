/* PX-BASIC, in the style of Palo Alto Tiny BASIC (Li-Chen Wang, 1976):
 * the program is kept as its text and interpreted from it, variables A-Z
 * and the array @(). The numbers are the calculator's own -- 34-digit
 * decimals, through px->dec*, so 0.1 + 0.2 is 0.3 and SIN is Plus42's.
 * Errors are the classic three: WHAT? (it can't be read), HOW? (it can't
 * be done), SORRY (no room). Everything else the machine does -- the
 * console, keys, screen, sound, files -- is bio.h's. */
#include "basic.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bio.h"
#include "px.h"

/* ===================== the program ===================== */

/* Line records, in line-number order: the number (2 bytes, low first),
 * the text's length (1 byte), the text (no 0 at its end). */
#define REC_HEAD 3
#define LINE_NUMBER_MAX 32767

static uint8_t prog[BASIC_PROG_MAX];
static int prog_len;
static bool changed;

static int rec_number(int at) {
    return prog[at] | prog[at + 1] << 8;
}

static int rec_len(int at) {
    return prog[at + 2];
}

static const char *rec_text(int at) {
    return (const char *) &prog[at + REC_HEAD];
}

static int rec_next(int at) {
    return at + REC_HEAD + rec_len(at);
}

/* The first line numbered n or more: its record, or prog_len. */
static int find_line(int n) {
    int at = 0;
    while (at < prog_len && rec_number(at) < n)
        at = rec_next(at);
    return at;
}

int basic_line_before(int n) {
    int best = 0;
    for (int at = 0; at < prog_len; at = rec_next(at)) {
        if (n != 0 && rec_number(at) >= n)
            break;
        best = rec_number(at);
    }
    return best;
}

int basic_line_after(int n) {
    int at = find_line(n + 1);
    return at < prog_len ? rec_number(at) : 0;
}

bool basic_line_get(int n, char *buf, int size) {
    int at = find_line(n);
    if (at >= prog_len || rec_number(at) != n)
        return false;
    snprintf(buf, (size_t) size, "%d %.*s", n, rec_len(at), rec_text(at));
    return true;
}

/* ===================== the state ===================== */

typedef px_dec_t num_t;

static num_t vars[26];
static num_t array[BASIC_ARRAY_MAX];
static int print_digits = 12; /* DIGITS: significant digits PRINT shows */
static bool degrees;          /* DEG / RAD */

/* Where it is: pc in the text of line record cur (-1: the line typed),
 * which ends at end. */
static const char *pc, *end;
static int cur;
static char typed[BASIC_LINE_MAX + 1];
static bool jumped; /* the statement just run went elsewhere (GOTO, NEXT...) */

/* A place to come back to: a line record (or -1) and where in its text. */
typedef struct {
    int line, pos;
} where_t;

#define STACK_MAX 32
static where_t gosubs[STACK_MAX];
static int gosub_count;

typedef struct {
    int var;
    num_t limit, step;
    where_t loop; /* just after the FOR */
} for_t;
static for_t fors[STACK_MAX];
static int for_count;

/* Ways a run ends early: the errors, R/S, and END (no message). */
enum { E_WHAT = 1, E_HOW, E_SORRY, E_BREAK, E_END };
static jmp_buf on_error;

static void fail(int e) {
    longjmp(on_error, e);
}

static void set_line(int at) {
    cur = at;
    pc = at < 0 ? typed : rec_text(at);
    end = at < 0 ? typed + strlen(typed) : pc + rec_len(at);
}

static where_t here(void) {
    return (where_t) {cur, (int) (pc - (cur < 0 ? typed : rec_text(cur)))};
}

static void go_back(where_t w) {
    set_line(w.line);
    pc += w.pos;
    jumped = true;
}

/* ===================== numbers ===================== */

/* A result that isn't a number -- too big, or no answer -- is HOW?. */
static num_t checked(num_t r) {
    if (px->dec_class(&r) != PX_DEC_FINITE)
        fail(E_HOW);
    return r;
}

static num_t num_int(int32_t v) {
    num_t r;
    px->dec_from_int(&r, v);
    return r;
}

static num_t op2(int op, num_t a, num_t b) {
    num_t r;
    px->dec(op, &r, &a, &b);
    return checked(r);
}

static num_t op1(int op, num_t a) {
    num_t r;
    px->dec(op, &r, &a, NULL);
    return checked(r);
}

static int cmp(num_t a, num_t b) {
    return px->dec_cmp(&a, &b);
}

static bool is_true(num_t a) {
    return cmp(a, num_int(0)) != 0;
}

static num_t truth(bool b) {
    return num_int(b ? 1 : 0);
}

static int32_t to_int(num_t a) {
    int32_t v;
    if (px->dec_to_int(&v, &a) != 0)
        fail(E_HOW);
    return v;
}

static num_t pi(void) {
    num_t r;
    px->dec_from_string(&r, "3.141592653589793238462643383279503");
    return r;
}

/* Angles in and out of the library, which works in radians. */
static num_t angle_in(num_t a) {
    return degrees ? op2(PX_DEC_DIV, op2(PX_DEC_MUL, a, pi()), num_int(180)) : a;
}

static num_t angle_out(num_t a) {
    return degrees ? op2(PX_DEC_DIV, op2(PX_DEC_MUL, a, num_int(180)), pi()) : a;
}

/* a MOD b: a - b * FLOOR(a / b), the sign of b's (as the HP-42S's MOD). */
static num_t mod(num_t a, num_t b) {
    if (!is_true(b))
        fail(E_HOW);
    return op2(PX_DEC_SUB, a, op2(PX_DEC_MUL, b, op1(PX_DEC_FLOOR, op2(PX_DEC_DIV, a, b))));
}

/* The number as PRINT shows it: rounded to `digits` significant digits,
 * no zeros at the end; plain unless that would take more than `digits`
 * digits or start with more than five zeros, then d.dddE-7. */
static int format_number(char *buf, size_t size, num_t v, int digits) {
    char s[PX_DEC_STRING_MAX];
    px->dec_to_string(s, &v); /* "+31415E-4": sign, digits, exponent */
    bool minus = s[0] == '-';
    const char *e = strchr(s, 'E');
    char d[PX_DEC_STRING_MAX + 2];
    int n = 0;
    for (const char *p = s + 1; p < e; p++)
        if (n > 0 || *p != '0')
            d[n++] = *p;
    if (n == 0)
        return snprintf(buf, size, "0");
    int q = atoi(e + 1); /* the value is d * 10^q */

    if (n > digits) { /* round half up, at the digit after the last kept */
        bool up = d[digits] >= '5';
        q += n - digits;
        n = digits;
        for (int i = n - 1; up && i >= 0; i--) {
            up = d[i] == '9';
            d[i] = up ? '0' : (char) (d[i] + 1);
        }
        if (up) { /* 999 -> 1000: one digit more, the last one goes */
            memmove(d + 1, d, (size_t) n - 1);
            d[0] = '1';
            q++;
        }
    }
    while (n > 1 && d[n - 1] == '0') {
        n--;
        q++;
    }
    d[n] = 0;
    int lead = q + n - 1; /* the first digit's power of ten */
    int len = 0;
#define PUT(...) (len += snprintf(buf + len, size > (size_t) len ? size - (size_t) len : 0, __VA_ARGS__))
    if (minus)
        PUT("-");
    if (lead >= digits || lead < -6) {
        PUT("%c", d[0]);
        if (n > 1)
            PUT(".%s", d + 1);
        PUT("E%d", lead);
    } else if (q >= 0) {
        PUT("%s%.*s", d, q, "0000000000000000000000000000000000");
    } else if (n + q > 0) {
        PUT("%.*s.%s", n + q, d, d + n + q);
    } else {
        PUT("0.%.*s%s", -(n + q), "000000", d);
    }
#undef PUT
    return len;
}

/* ===================== reading the text ===================== */

static void skip(void) {
    while (pc < end && *pc == ' ')
        pc++;
}

/* At the end of a statement: the line's end, or ':'. */
static bool at_end(void) {
    skip();
    return pc >= end || *pc == ':';
}

static bool is_char(char c) {
    skip();
    if (pc < end && *pc == c) {
        pc++;
        return true;
    }
    return false;
}

static void expect(char c) {
    if (!is_char(c))
        fail(E_WHAT);
}

static bool is_word(const char *w) {
    skip();
    size_t n = strlen(w);
    if ((size_t) (end - pc) >= n && memcmp(pc, w, n) == 0) {
        pc += n;
        return true;
    }
    return false;
}

static bool at_letter(void) {
    skip();
    return pc < end && *pc >= 'A' && *pc <= 'Z';
}

static bool at_digit(void) {
    skip();
    return pc < end && (isdigit((unsigned char) *pc) || (*pc == '.' && pc + 1 < end && isdigit((unsigned char) pc[1])));
}

/* "..." : its text, *len long; pc after the closing quote. */
static const char *string(int *len) {
    expect('"');
    const char *s = pc;
    while (pc < end && *pc != '"')
        pc++;
    *len = (int) (pc - s);
    expect('"');
    return s;
}

/* 12, 1.5, .5, 2E-3: an E is the exponent only with digits after it --
 * "2*E" is two times E. */
static num_t number(void) {
    char buf[48];
    int n = 0;
    while (pc < end && (isdigit((unsigned char) *pc) || *pc == '.') && n < 40)
        buf[n++] = *pc++;
    if (pc < end && *pc == 'E') {
        const char *digits = pc + 1; /* after the E and its sign */
        if (digits < end && (*digits == '+' || *digits == '-'))
            digits++;
        if (digits < end && isdigit((unsigned char) *digits)) {
            while (pc < digits)
                buf[n++] = *pc++;
            while (pc < end && isdigit((unsigned char) *pc) && n < 46)
                buf[n++] = *pc++;
        }
    }
    buf[n] = 0;
    num_t r;
    if (px->dec_from_string(&r, buf) != 0)
        fail(E_WHAT);
    return r;
}

/* ===================== expressions ===================== */

static num_t expr(void);

static num_t in_parens(void) {
    expect('(');
    num_t v = expr();
    expect(')');
    return v;
}

static int array_index(void) {
    int32_t i = to_int(in_parens());
    if (i < 0 || i >= BASIC_ARRAY_MAX)
        fail(E_HOW);
    return (int) i;
}

/* The functions of one argument: name( with its "(", what it does. */
static const struct {
    const char *name;
    int op;
    int angle; /* 1: takes an angle, 2: gives one */
} functions[] = {
        {"ABS(", PX_DEC_ABS, 0},  {"INT(", PX_DEC_FLOOR, 0}, {"SQR(", PX_DEC_SQRT, 0},
        {"EXP(", PX_DEC_EXP, 0},  {"LOG10(", PX_DEC_LOG10, 0}, {"LOG(", PX_DEC_LN, 0},
        {"SIN(", PX_DEC_SIN, 1},  {"COS(", PX_DEC_COS, 1},   {"TAN(", PX_DEC_TAN, 1},
        {"ASN(", PX_DEC_ASIN, 2}, {"ACS(", PX_DEC_ACOS, 2},  {"ATN(", PX_DEC_ATAN, 2},
};

static num_t power(void);

static num_t factor(void) {
    skip();
    if (at_digit())
        return number();
    if (is_char('-'))
        return op1(PX_DEC_NEG, power());
    if (is_char('+'))
        return power();
    if (pc < end && *pc == '(')
        return in_parens();
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        size_t n = strlen(functions[i].name);
        if ((size_t) (end - pc) >= n && memcmp(pc, functions[i].name, n) == 0) {
            pc += n - 1; /* in_parens() takes the "(" */
            num_t a = in_parens();
            num_t r = op1(functions[i].op, functions[i].angle == 1 ? angle_in(a) : a);
            return functions[i].angle == 2 ? angle_out(r) : r;
        }
    }
    if (is_word("SGN")) {
        int c = cmp(in_parens(), num_int(0));
        return num_int(c);
    }
    if (is_word("RND")) {
        int32_t n = to_int(in_parens());
        if (n < 0)
            fail(E_HOW);
        if (n == 0) { /* RND(0): a fraction, 0 up to 1 */
            num_t r = op2(PX_DEC_DIV, num_int((int32_t) (bio_random() >> 1)), num_int(INT32_MAX));
            return r;
        }
        return num_int((int32_t) (bio_random() % (uint32_t) n) + 1);
    }
    if (is_word("PI"))
        return pi();
    if (is_word("KEY")) {
        if (is_char('('))
            expect(')');
        return num_int(bio_key());
    }
    if (is_word("SIZE"))
        return num_int(basic_free());
    if (is_char('@'))
        return array[array_index()];
    if (at_letter())
        return vars[*pc++ - 'A'];
    fail(E_WHAT);
    return num_int(0);
}

/* a ^ b, right to left: 2^3^2 is 2^9. */
static num_t power(void) {
    num_t a = factor();
    if (is_char('^'))
        return op2(PX_DEC_POW, a, power());
    return a;
}

static num_t term(void) {
    num_t v = power();
    while (true) {
        if (is_char('*')) {
            v = op2(PX_DEC_MUL, v, power());
        } else if (is_char('/')) {
            num_t d = power();
            if (!is_true(d))
                fail(E_HOW);
            v = op2(PX_DEC_DIV, v, d);
        } else if (is_word("MOD")) {
            v = mod(v, power());
        } else {
            return v;
        }
    }
}

static num_t sum(void) {
    num_t v = term();
    while (true) {
        if (is_char('+'))
            v = op2(PX_DEC_ADD, v, term());
        else if (is_char('-'))
            v = op2(PX_DEC_SUB, v, term());
        else
            return v;
    }
}

static num_t relation(void) {
    num_t a = sum();
    if (is_word("<="))
        return truth(cmp(a, sum()) <= 0);
    if (is_word(">="))
        return truth(cmp(a, sum()) >= 0);
    if (is_word("<>"))
        return truth(cmp(a, sum()) != 0);
    if (is_char('<'))
        return truth(cmp(a, sum()) < 0);
    if (is_char('>'))
        return truth(cmp(a, sum()) > 0);
    if (is_char('='))
        return truth(cmp(a, sum()) == 0);
    return a;
}

static num_t negation(void) {
    if (is_word("NOT"))
        return truth(!is_true(negation()));
    return relation();
}

static num_t conjunction(void) {
    num_t v = negation();
    while (is_word("AND")) {
        num_t w = negation();
        v = truth(is_true(v) && is_true(w));
    }
    return v;
}

static num_t expr(void) {
    num_t v = conjunction();
    while (is_word("OR")) {
        num_t w = conjunction();
        v = truth(is_true(v) || is_true(w));
    }
    return v;
}

static int32_t int_expr(void) {
    return to_int(expr());
}

/* ===================== output ===================== */

static void out(const char *s) {
    bio_out(s, (int) strlen(s));
}

static void out_number(num_t v) {
    char buf[64];
    bio_out(buf, format_number(buf, sizeof(buf), v, print_digits));
}

static void out_int(int v) {
    char buf[16];
    bio_out(buf, snprintf(buf, sizeof(buf), "%d", v));
}

static void new_line_if_needed(void) {
    if (bio_column() != 0)
        out("\n");
}

static void list_line(int at) {
    char buf[BASIC_LINE_MAX + 16];
    bio_out(buf, snprintf(buf, sizeof(buf), "%d %.*s\n", rec_number(at), rec_len(at), rec_text(at)));
}

/* ===================== the program, changed ===================== */

static void clear_run_state(void) {
    gosub_count = 0;
    for_count = 0;
}

static void clear_vars(void) {
    for (int i = 0; i < 26; i++)
        vars[i] = num_int(0);
    for (int i = 0; i < BASIC_ARRAY_MAX; i++)
        array[i] = vars[0];
}

/* Line n becomes text (len 0: line n goes). */
static void store_line(int n, const char *text, int len) {
    int at = find_line(n);
    if (at < prog_len && rec_number(at) == n) {
        int next = rec_next(at);
        memmove(prog + at, prog + next, (size_t) (prog_len - next));
        prog_len -= next - at;
    }
    clear_run_state();
    changed = true;
    if (len == 0)
        return;
    if (prog_len + REC_HEAD + len > BASIC_PROG_MAX)
        fail(E_SORRY);
    memmove(prog + at + REC_HEAD + len, prog + at, (size_t) (prog_len - at));
    prog[at] = (uint8_t) n;
    prog[at + 1] = (uint8_t) (n >> 8);
    prog[at + 2] = (uint8_t) len;
    memcpy(prog + at + REC_HEAD, text, (size_t) len);
    prog_len += REC_HEAD + len;
}

void basic_new(void) {
    prog_len = 0;
    clear_vars();
    clear_run_state();
    changed = true;
}

int basic_free(void) {
    return BASIC_PROG_MAX - prog_len;
}

bool basic_changed(void) {
    return changed;
}

void basic_saved(void) {
    changed = false;
}

/* Upper case, except inside "..."; control characters as spaces; no
 * spaces at the end. Returns the length. */
static int normalize(char *s) {
    bool quoted = false;
    int n = 0;
    for (char *p = s; *p; p++, n++) {
        if (*p == '"')
            quoted = !quoted;
        else if (!quoted)
            *p = (char) toupper((unsigned char) *p);
        if ((unsigned char) *p < ' ')
            *p = ' ';
    }
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = 0;
    return n;
}

/* A line with a number into the program: false if it has none. */
static bool program_line(const char *s) {
    const char *p = s;
    while (*p == ' ')
        p++;
    if (!isdigit((unsigned char) *p))
        return false;
    long n = 0;
    while (isdigit((unsigned char) *p)) {
        n = n * 10 + (*p++ - '0');
        if (n > LINE_NUMBER_MAX)
            fail(E_HOW);
    }
    if (n == 0)
        fail(E_HOW);
    while (*p == ' ')
        p++;
    store_line((int) n, p, (int) strlen(p));
    return true;
}

/* ===================== files ===================== */

/* "NAME", or NAME without the quotes -- in capitals: in quotes it's as
 * typed, and the drive doesn't tell "Ring" from "RING" anyway. */
static void file_name(char *name) {
    int len;
    const char *s;
    skip();
    if (pc < end && *pc == '"') {
        s = string(&len);
    } else {
        s = pc;
        while (pc < end && *pc != ' ' && *pc != ':')
            pc++;
        len = (int) (pc - s);
    }
    if (!at_end()) /* LOAD RING 3D: not a program called RING */
        fail(E_WHAT);
    if (len == 0 || len > BIO_NAME_MAX)
        fail(E_HOW);
    for (int i = 0; i < len; i++) {
        char c = (char) toupper((unsigned char) s[i]);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            fail(E_HOW);
        name[i] = c;
    }
    name[len] = 0;
}

static void save(const char *name) {
    int f = bio_open(name, true);
    if (f < 0)
        fail(E_HOW);
    bool ok = true;
    for (int at = 0; at < prog_len && ok; at = rec_next(at)) {
        char buf[BASIC_LINE_MAX + 16];
        int n = snprintf(buf, sizeof(buf), "%d %.*s\n", rec_number(at), rec_len(at), rec_text(at));
        ok = bio_write(f, buf, n) == n;
    }
    if (!bio_close(f) || !ok)
        fail(E_HOW);
    changed = false;
}

/* One line of a file into the program: 0, or the error (the caller closes
 * the file before failing). Lines without a number are left out. */
static int load_line(char *line, bool too_long) {
    if (too_long)
        return E_HOW;
    int n = normalize(line);
    if (n == 0)
        return 0;
    long number = strtol(line, NULL, 10);
    if (isdigit((unsigned char) line[strspn(line, " ")]) && (number < 1 || number > LINE_NUMBER_MAX))
        return E_HOW;
    if (prog_len + REC_HEAD + n > BASIC_PROG_MAX)
        return E_SORRY;
    program_line(line);
    return 0;
}

static void load(const char *name) {
    int f = bio_open(name, false);
    if (f < 0)
        fail(E_HOW);
    basic_new();
    char buf[256], line[BASIC_LINE_MAX + 1];
    int len = 0, n, problem = 0;
    bool too_long = false;
    while (!problem && (n = bio_read(f, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n && !problem; i++) {
            if (buf[i] != '\n' && buf[i] != '\r') {
                if (len < BASIC_LINE_MAX)
                    line[len++] = buf[i];
                else
                    too_long = true;
                continue;
            }
            line[len] = 0;
            problem = load_line(line, too_long);
            len = 0;
            too_long = false;
        }
    }
    if (!problem && (len > 0 || too_long)) { /* a last line with no end */
        line[len] = 0;
        problem = load_line(line, too_long);
    }
    bio_close(f);
    if (problem)
        fail(problem);
    changed = false;
}

static void files(void) {
    static char names[64][BIO_NAME_MAX + 1];
    int count = bio_files(names, 64);
    for (int i = 0; i < count; i++) {
        if (bio_column() + (int) strlen(names[i]) > 30)
            out("\n");
        out(names[i]);
        int pad = 10 - bio_column() % 10;
        if (bio_column() + pad <= 30)
            bio_out("          ", pad);
        else
            out("\n");
    }
    new_line_if_needed();
}

/* ===================== statements ===================== */

typedef struct {
    bool in_array;
    int index;
} target_t;

static target_t target(void) {
    if (is_char('@'))
        return (target_t) {true, array_index()};
    if (!at_letter())
        fail(E_WHAT);
    return (target_t) {false, *pc++ - 'A'};
}

static void assign(target_t t, num_t v) {
    if (t.in_array)
        array[t.index] = v;
    else
        vars[t.index] = v;
}

static void go_to_line(int32_t n) {
    int at = find_line(n);
    if (at >= prog_len || rec_number(at) != n)
        fail(E_HOW);
    set_line(at);
    jumped = true;
}

static void st_print(void) {
    bool new_line = true;
    while (!at_end()) {
        new_line = true;
        if (*pc == '"') {
            int len;
            const char *s = string(&len);
            bio_out(s, len);
        } else {
            out_number(expr());
        }
        if (is_char(';')) {
            new_line = false;
        } else if (is_char(',')) {
            bio_out("        ", 8 - bio_column() % 8);
            new_line = false;
        } else {
            break;
        }
    }
    if (!at_end())
        fail(E_WHAT);
    if (new_line)
        out("\n");
}

/* A number typed for INPUT, with spaces around it: false if it isn't one. */
static bool typed_number(char *s, num_t *v) {
    while (*s == ' ')
        s++;
    int n = (int) strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = 0;
    return n > 0 && px->dec_from_string(v, s) == 0 && px->dec_class(v) == PX_DEC_FINITE;
}

static void st_input(void) {
    do {
        const char *prompt = NULL;
        int prompt_len = 0;
        skip();
        if (pc < end && *pc == '"') {
            prompt = string(&prompt_len);
            if (!is_char(',') && !is_char(';'))
                fail(E_WHAT);
        }
        skip();
        const char *name = pc;
        target_t t = target();
        int name_len = (int) (pc - name);
        char line[BASIC_LINE_MAX + 1];
        num_t v;
        while (true) {
            if (prompt != NULL) {
                bio_out(prompt, prompt_len);
            } else {
                bio_out(name, name_len);
                out("? ");
            }
            if (bio_input(line, sizeof(line)) < 0)
                fail(E_BREAK);
            if (typed_number(line, &v))
                break;
            out("HOW?\n");
        }
        assign(t, v);
    } while (is_char(','));
}

static void statement(void);

static void st_if(void) {
    bool c = is_true(expr());
    is_word("THEN");
    if (!c) {
        pc = end; /* the rest of the line too */
        return;
    }
    if (at_digit())
        go_to_line(int_expr());
    else
        statement();
}

static void st_for(void) {
    target_t t = target();
    if (t.in_array)
        fail(E_WHAT);
    expect('=');
    vars[t.index] = expr();
    if (!is_word("TO"))
        fail(E_WHAT);
    num_t limit = expr();
    num_t step = is_word("STEP") ? expr() : num_int(1);
    for (int i = for_count - 1; i >= 0; i--)
        if (fors[i].var == t.index) { /* the loop again: the old one goes */
            for_count = i;
            break;
        }
    if (for_count == STACK_MAX)
        fail(E_SORRY);
    fors[for_count++] = (for_t) {t.index, limit, step, here()};
}

static void st_next(void) {
    int var = -1;
    if (at_letter())
        var = *pc++ - 'A';
    int i = for_count - 1;
    while (i >= 0 && var >= 0 && fors[i].var != var)
        i--;
    if (i < 0)
        fail(E_HOW);
    for_count = i + 1;
    for_t *f = &fors[i];
    num_t v = vars[f->var] = op2(PX_DEC_ADD, vars[f->var], f->step);
    bool up = cmp(f->step, num_int(0)) >= 0;
    if (up ? cmp(v, f->limit) <= 0 : cmp(v, f->limit) >= 0)
        go_back(f->loop);
    else
        for_count = i;
}

static void st_gosub(void) {
    int32_t n = int_expr();
    if (gosub_count == STACK_MAX)
        fail(E_SORRY);
    gosubs[gosub_count++] = here();
    go_to_line(n);
}

static void st_return(void) {
    if (gosub_count == 0)
        fail(E_HOW);
    go_back(gosubs[--gosub_count]);
}

static void st_list(void) {
    int from = 1, to = LINE_NUMBER_MAX;
    if (!at_end()) {
        from = (int) int_expr();
        to = is_char(',') ? (int) int_expr() : LINE_NUMBER_MAX;
    }
    for (int at = find_line(from); at < prog_len && rec_number(at) <= to; at = rec_next(at)) {
        if (bio_stop())
            fail(E_BREAK);
        list_line(at);
    }
}

static void st_run(void) {
    clear_vars();
    clear_run_state();
    if (prog_len == 0)
        fail(E_END);
    set_line(0);
    jumped = true;
}

/* need to max whole numbers, "," between them. Returns how many. */
static int numbers(int32_t *v, int need, int max) {
    int got = 0;
    v[got++] = int_expr();
    while (got < max && is_char(','))
        v[got++] = int_expr();
    if (got < need)
        fail(E_WHAT);
    return got;
}

static void statement(void) {
    skip();
    if (pc >= end || *pc == ':')
        return;
    if (is_word("REM")) {
        pc = end;
    } else if (is_word("PRINT") || is_char('?')) {
        st_print();
    } else if (is_word("INPUT")) {
        st_input();
    } else if (is_word("IF")) {
        st_if();
    } else if (is_word("GOTO")) {
        go_to_line(int_expr());
    } else if (is_word("GOSUB")) {
        st_gosub();
    } else if (is_word("RETURN")) {
        st_return();
    } else if (is_word("FOR")) {
        st_for();
    } else if (is_word("NEXT")) {
        st_next();
    } else if (is_word("END") || is_word("STOP")) {
        fail(E_END);
    } else if (is_word("LET")) {
        target_t t = target();
        expect('=');
        assign(t, expr());
    } else if (is_word("CLS")) {
        bio_cls();
    } else if (is_word("AT")) {
        int32_t v[2];
        numbers(v, 2, 2);
        bio_at((int) v[0], (int) v[1]);
    } else if (is_word("PSET")) {
        int32_t v[3] = {0, 0, 1};
        numbers(v, 2, 3);
        bio_pset((int) v[0], (int) v[1], v[2] != 0);
    } else if (is_word("LINE")) {
        int32_t v[5] = {0, 0, 0, 0, 1};
        numbers(v, 4, 5);
        bio_line((int) v[0], (int) v[1], (int) v[2], (int) v[3], v[4] != 0);
    } else if (is_word("BOX")) {
        int32_t v[5] = {0, 0, 0, 0, 1};
        numbers(v, 4, 5);
        bio_box((int) v[0], (int) v[1], (int) v[2], (int) v[3], v[4] != 0);
    } else if (is_word("TONE")) {
        int32_t t = int_expr();
        if (t < 0 || t > 10)
            fail(E_HOW);
        bio_tone((int) t);
    } else if (is_word("WAIT")) {
        int32_t ms = int_expr();
        if (ms < 0)
            fail(E_HOW);
        if (!bio_wait((int) ms))
            fail(E_BREAK);
    } else if (is_word("DIGITS")) {
        int32_t d = int_expr();
        if (d < 1 || d > 34)
            fail(E_HOW);
        print_digits = (int) d;
    } else if (is_word("DEG")) {
        degrees = true;
    } else if (is_word("RAD")) {
        degrees = false;
    } else if (is_word("LIST")) {
        st_list();
    } else if (is_word("RUN")) {
        st_run();
    } else if (is_word("NEW")) {
        basic_new();
        fail(E_END);
    } else if (is_word("SAVE")) {
        char name[BIO_NAME_MAX + 1];
        file_name(name);
        save(name);
    } else if (is_word("LOAD")) {
        char name[BIO_NAME_MAX + 1];
        file_name(name);
        load(name);
        fail(E_END);
    } else if (is_word("FILES")) {
        files();
    } else if (is_word("ERASE")) {
        char name[BIO_NAME_MAX + 1];
        file_name(name);
        if (!bio_remove(name))
            fail(E_HOW);
    } else if (at_letter() || *pc == '@') {
        target_t t = target();
        expect('=');
        assign(t, expr());
    } else {
        fail(E_WHAT);
    }
}

/* Statements from pc on, line after line, until the end of the program
 * (or of the line typed), END or an error. */
static void run(void) {
    while (true) {
        if (bio_stop())
            fail(E_BREAK);
        jumped = false;
        statement();
        if (jumped)
            continue;
        if (is_char(':'))
            continue;
        if (pc < end)
            fail(E_WHAT);
        if (cur < 0)
            return;
        int next = rec_next(cur);
        if (next >= prog_len)
            return;
        set_line(next);
    }
}

void basic_enter(const char *line) {
    static bool started;
    if (!started) { /* the variables are zeros only once px is there */
        clear_vars();
        started = true;
    }
    snprintf(typed, sizeof(typed), "%s", line);
    normalize(typed);
    set_line(-1);
    int e = setjmp(on_error);
    if (e == 0) {
        if (!program_line(typed))
            run();
        return;
    }
    if (e == E_END)
        return;
    static const char *const messages[] = {[E_WHAT] = "WHAT?", [E_HOW] = "HOW?", [E_SORRY] = "SORRY",
                                           [E_BREAK] = "BREAK"};
    new_line_if_needed();
    out(messages[e]);
    if (cur >= 0) {
        out(" IN ");
        out_int(rec_number(cur));
    }
    out("\n");
}
