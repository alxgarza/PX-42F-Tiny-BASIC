/* The simulator's decimal numbers (px_app.h): a double kept in the 16
 * bytes of a px_dec_t, with the C library's math. The calculator's are
 * decimal128, 34 digits: here results agree to about 16 digits, and
 * 0.1 + 0.2 isn't quite 0.3 -- round when printing. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_dec.h"

static double get(const px_dec_t *d) {
    double v;
    memcpy(&v, d, sizeof(v));
    return v;
}

static void put(px_dec_t *d, double v) {
    memset(d, 0, sizeof(*d));
    memcpy(d, &v, sizeof(v));
}

int sim_dec_op(int op, px_dec_t *r, const px_dec_t *a, const px_dec_t *b) {
    double x = get(a), y = b != NULL ? get(b) : 0, z;
    switch (op) {
        case PX_DEC_ADD: z = x + y; break;
        case PX_DEC_SUB: z = x - y; break;
        case PX_DEC_MUL: z = x * y; break;
        case PX_DEC_DIV: z = x / y; break;
        case PX_DEC_POW: z = pow(x, y); break;
        case PX_DEC_REM: z = remainder(x, y); break;
        case PX_DEC_ATAN2: z = atan2(x, y); break;
        case PX_DEC_HYPOT: z = hypot(x, y); break;
        case PX_DEC_NEG: z = -x; break;
        case PX_DEC_ABS: z = fabs(x); break;
        case PX_DEC_SQRT: z = sqrt(x); break;
        case PX_DEC_EXP: z = exp(x); break;
        case PX_DEC_LN: z = log(x); break;
        case PX_DEC_LOG10: z = log10(x); break;
        case PX_DEC_EXPM1: z = expm1(x); break;
        case PX_DEC_LN1P: z = log1p(x); break;
        case PX_DEC_SIN: z = sin(x); break;
        case PX_DEC_COS: z = cos(x); break;
        case PX_DEC_TAN: z = tan(x); break;
        case PX_DEC_ASIN: z = asin(x); break;
        case PX_DEC_ACOS: z = acos(x); break;
        case PX_DEC_ATAN: z = atan(x); break;
        case PX_DEC_SINH: z = sinh(x); break;
        case PX_DEC_COSH: z = cosh(x); break;
        case PX_DEC_TANH: z = tanh(x); break;
        case PX_DEC_ASINH: z = asinh(x); break;
        case PX_DEC_ACOSH: z = acosh(x); break;
        case PX_DEC_ATANH: z = atanh(x); break;
        case PX_DEC_GAMMA: z = tgamma(x); break;
        case PX_DEC_FLOOR: z = floor(x); break;
        case PX_DEC_CEIL: z = ceil(x); break;
        case PX_DEC_TRUNC: z = trunc(x); break;
        default: return -1;
    }
    put(r, z);
    return 0;
}

void sim_dec_from_int(px_dec_t *r, int32_t v) {
    put(r, v);
}

int sim_dec_class(const px_dec_t *a) {
    double x = get(a);
    return isnan(x) ? PX_DEC_NAN : isinf(x) ? PX_DEC_INF : PX_DEC_FINITE;
}

int sim_dec_to_int(int32_t *r, const px_dec_t *a) {
    double t = trunc(get(a));
    if (!(t >= INT32_MIN && t <= INT32_MAX))
        return -1;
    *r = (int32_t) t;
    return 0;
}

int sim_dec_from_string(px_dec_t *r, const char *s) {
    char *end;
    double v = strtod(s, &end);
    /* As the library: too big is infinity, and still a success -- only
     * text that isn't a number is not. */
    if (end == s || *end != 0 || isnan(v)) {
        put(r, NAN);
        return -1;
    }
    put(r, v);
    return 0;
}

/* As the library writes them: "+12345E-4" -- the digits, then the
 * exponent; 17 significant digits, trailing zeros dropped. */
void sim_dec_to_string(char buf[PX_DEC_STRING_MAX], const px_dec_t *a) {
    double x = get(a);
    if (isnan(x) || isinf(x)) {
        snprintf(buf, PX_DEC_STRING_MAX, "%sInf", signbit(x) ? "-" : "+");
        if (isnan(x))
            snprintf(buf, PX_DEC_STRING_MAX, "+NaN");
        return;
    }
    char e[32];
    snprintf(e, sizeof(e), "%.16e", fabs(x)); /* d.dddddddddddddddde+NN */
    char digits[20];
    int n = 0;
    for (const char *p = e; *p && *p != 'e'; p++)
        if (*p != '.')
            digits[n++] = *p;
    int exponent = atoi(strchr(e, 'e') + 1) - (n - 1);
    while (n > 1 && digits[n - 1] == '0') {
        n--;
        exponent++;
    }
    if (x == 0)
        exponent = 0;
    snprintf(buf, PX_DEC_STRING_MAX, "%c%.*sE%d", signbit(x) ? '-' : '+', n, digits, exponent);
}

int sim_dec_cmp(const px_dec_t *a, const px_dec_t *b) {
    double x = get(a), y = get(b);
    if (isnan(x) || isnan(y))
        return 2;
    return x < y ? -1 : x > y ? 1 : 0;
}
