/* The PX-42F on a Mac: the app's own code runs against this table instead
 * of the firmware's, on a virtual clock, driven by a key script, and the
 * screen is saved as PNGs -- to try an app, and look at it, without the
 * calculator. Drawing uses the calculator's fonts (sim/fonts, copied from
 * the firmware) and its PNG writer.
 *
 *   build/sim OUT_DIR DRIVE_DIR [WORD...]
 *
 * DRIVE_DIR stands for the calculator's drive ("make sim" copies sim/drive
 * there first); the app's folder is SIM_APP_DIR in it (the Makefile's KIND
 * and NAME). Words, in order:
 *   R0C0 .. R6C5      press that key (down, then up on the next key() call);
 *                     also SIGMA, XEQ, ENTER, BACKSPACE, UP, DOWN, SHIFT, RS
 *   ON                ON: ends the app (key() returns PX_KEY_QUIT) -- or,
 *                     after on_key(1), a key like the others (PX_KEY_ON)
 *   QUIT              end the app, as the Sleep Timer does (when it next
 *                     waits for a key: a running program finishes first)
 *   250               let 250 ms pass
 *   shot:NAME         save what show() last sent as OUT_DIR/NAME.png
 *   serial:TEXT       TEXT arrives on the serial port, then Enter (a CR);
 *                     "_" in it stands for a space
 * Time is virtual: it moves on through the pauses, and by 1 ms each time
 * the app asks for it (millis()) -- an app that computes without waiting
 * still sees time pass, and its key checks still come round.
 * After the last word the app is ended as by QUIT. Sounds, the serial port's
 * output and IR transmissions are printed.
 *
 *   build/sim --live DRIVE_DIR FRAME_FD
 *
 * Live, for sim/live.py: the clock is the computer's, and instead of a script
 * the lines on stdin say what happens -- "down NAME" and "up NAME" (a key, as
 * above, or ON), "serial TEXT" (then a CR), "quit". The screen goes to
 * FRAME_FD each time show() is called (at most every 16 ms, and when the app
 * waits): first the line "W H STRIDE", then messages: "F" and a frame
 * (PX_SCREEN_H rows of PX_SCREEN_STRIDE bytes), "S" and a path on the drive
 * and a newline (that sound starts), "X" (the sound stops). */
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fonts/font_10x16.h"
#include "fonts/font_6x8.h"
#include "fonts/font_8x12.h"
#include "pngwrite.h"
#include "sim_dec.h"
#include "px.h"

#ifndef SIM_APP_DIR
#define SIM_APP_DIR "/BIN/APPS/APP"
#endif

const px_api_t *px;

static uint8_t screen[PX_SCREEN_H * PX_SCREEN_STRIDE];
static uint8_t shown[PX_SCREEN_H * PX_SCREEN_STRIDE];
static const char *out_dir, *drive_dir;
static char **words;
static int word_count, next_word;
static uint32_t now_ms, word_left_ms;
static int pending_up = -1;
static bool on_is_key;
static uint32_t seed = 12345;
static FILE *files[PX_FILES_OPEN];
static char serial_in[4096];
static size_t serial_in_len, serial_in_pos;
static bool serial_arrived; /* key() returns PX_KEY_NONE once, for the app to look */

/* Live: what stdin has said and the screen not yet sent. */
#define FRAME_EVERY_MS 16
#define EVENTS_MAX     64
static bool live, input_open = true, quit_asked, frame_dirty;
static int frame_fd;
static struct timespec live_start;
static uint32_t last_frame_ms;
static int events[EVENTS_MAX], event_first, event_count;
static char input_line[1200];
static size_t input_len;

/* The time to print: live, it's the computer's. */
static uint32_t live_clock(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    now_ms = (uint32_t) ((t.tv_sec - live_start.tv_sec) * 1000 + (t.tv_nsec - live_start.tv_nsec) / 1000000);
    return now_ms;
}

static uint32_t stamp(void) {
    return live ? live_clock() : now_ms;
}

/* ----- the screen ----- */

/* One message to the page: a letter, then len bytes. */
static void emit(char tag, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *) &tag;
    size_t left = 1;
    for (int part = 0; part < 2; part++) {
        while (left > 0) {
            ssize_t n = write(frame_fd, p, left);
            if (n <= 0) {
                quit_asked = true; /* nobody is looking any more */
                return;
            }
            p += n;
            left -= (size_t) n;
        }
        p = data;
        left = len;
    }
}

static void flush_frame(void) {
    if (!frame_dirty)
        return;
    frame_dirty = false;
    emit('F', shown, sizeof(shown));
}

static void sim_show(void) {
    memcpy(shown, screen, sizeof(shown));
    if (live) {
        frame_dirty = true;
        if (live_clock() - last_frame_ms >= FRAME_EVERY_MS) {
            last_frame_ms = now_ms;
            flush_frame();
        }
    }
}

static void sim_clear(void) {
    memset(screen, 0, sizeof(screen));
}

static void sim_pixel(int x, int y, int black) {
    if (x < 0 || x >= PX_SCREEN_W || y < 0 || y >= PX_SCREEN_H)
        return;
    uint8_t *byte = &screen[y * PX_SCREEN_STRIDE + x / 8];
    uint8_t mask = (uint8_t) (0x80 >> (x % 8));
    *byte = black ? (uint8_t) (*byte | mask) : (uint8_t) (*byte & ~mask);
}

static int sim_get_pixel(int x, int y) {
    if (x < 0 || x >= PX_SCREEN_W || y < 0 || y >= PX_SCREEN_H)
        return 0;
    return screen[y * PX_SCREEN_STRIDE + x / 8] >> (7 - x % 8) & 1;
}

static void sim_fill(int x, int y, int w, int h, int black) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            sim_pixel(i, j, black);
}

static void sim_line(int x0, int y0, int x1, int y1, int black) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        sim_pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

static void sim_bitmap(int x, int y, int w, int h, const uint8_t *bits, int stride) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (bits[j * stride + i / 8] & (0x80 >> (i % 8)))
                sim_pixel(x + i, y + j, 1);
}

/* The chosen fonts are the built-in ones here. */
static const display_font_t *font_by_number(int font) {
    static const display_font_t *const fonts[] = {&font_8x12, &font_10x16, &font_6x8};
    return font >= 0 && font <= PX_FONT_CHOSEN_MINI ? fonts[font % 3] : NULL;
}

/* As the firmware's: each glyph's whole cell, its bits XORed with inverse. */
static int sim_text(int x, int y, const char *s, int font, int inverse) {
    const display_font_t *f = font_by_number(font);
    if (f == NULL)
        return x;
    for (; *s; s++) {
        uint8_t c = (uint8_t) *s;
        if (c >= f->first_char && c <= f->last_char) {
            const uint8_t *glyph = f->data + (c - f->first_char) * f->bytes_per_char;
            for (int bit = 0; bit < f->width * f->height; bit++) {
                int on = (glyph[bit / 8] >> (7 - bit % 8)) & 1;
                sim_pixel(x + bit % f->width, y + bit / f->width, on ^ (inverse != 0));
            }
        }
        x += f->advance;
    }
    return x;
}

static int sim_text_width(const char *s, int font) {
    const display_font_t *f = font_by_number(font);
    return f == NULL ? 0 : (int) strlen(s) * f->advance;
}

static int sim_font_height(int font) {
    const display_font_t *f = font_by_number(font);
    return f == NULL ? 0 : f->height;
}

/* ----- the key script and the clock ----- */

static int key_by_name(const char *w) {
    static const struct {
        const char *name;
        int key;
    } named[] = {
            {"SIGMA", PX_KEY_SIGMA},         {"XEQ", PX_KEY_XEQ},          {"ENTER", PX_KEY_ENTER},
            {"BACKSPACE", PX_KEY_BACKSPACE}, {"UP", PX_KEY_ARROW_UP},      {"DOWN", PX_KEY_ARROW_DN},
            {"SHIFT", PX_KEY_SHIFT},         {"RS", PX_KEY_RS},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
        if (strcasecmp(w, named[i].name) == 0)
            return named[i].key;
    int r, c;
    char end;
    if (sscanf(w, "R%dC%d%c", &r, &c, &end) == 2 && r >= 0 && r <= 6 && c >= 0 && c <= 5)
        return PX_KEY(r, c);
    return -1;
}

static bool write_file(void *ctx, const uint8_t *data, size_t len) {
    return fwrite(data, 1, len, ctx) == len;
}

static void save_shot(const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s.png", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL || !png_write_1bit(shown, PX_SCREEN_W, PX_SCREEN_H, PX_SCREEN_STRIDE, write_file, f)) {
        fprintf(stderr, "%s: can't write it\n", path);
        exit(1);
    }
    fclose(f);
    printf("%6u ms  %s\n", now_ms, path);
}

static bool is_time(const char *w, long *ms) {
    char *end;
    *ms = strtol(w, &end, 10);
    return end != w && *end == 0;
}

/* Lets up to ms pass (< 0: until a key), taking shots on the way; stops at
 * a key or the end of the script. Returns the ms that passed. */
static uint32_t pass_time(int ms) {
    uint32_t passed = 0;
    long t;
    while (next_word < word_count && (ms < 0 || passed < (uint32_t) ms)) {
        const char *w = words[next_word];
        if (strncmp(w, "shot:", 5) == 0) {
            save_shot(w + 5);
            next_word++;
        } else if (strncmp(w, "serial:", 7) == 0) {
            for (const char *s = w + 7; *s && serial_in_len < sizeof(serial_in) - 1; s++)
                serial_in[serial_in_len++] = *s == '_' ? ' ' : *s;
            if (serial_in_len < sizeof(serial_in))
                serial_in[serial_in_len++] = '\r';
            next_word++;
            serial_arrived = true;
            break;
        } else if (is_time(w, &t)) {
            if (word_left_ms == 0)
                word_left_ms = (uint32_t) t;
            uint32_t step = ms < 0 || (uint32_t) ms - passed >= word_left_ms ? word_left_ms : (uint32_t) ms - passed;
            now_ms += step;
            passed += step;
            word_left_ms -= step;
            if (word_left_ms == 0)
                next_word++;
        } else {
            break;
        }
    }
    return passed;
}

static void push_event(int key) {
    if (event_count < EVENTS_MAX)
        events[(event_first + event_count++) % EVENTS_MAX] = key;
}

/* One line from stdin: "down NAME", "up NAME", "serial TEXT" or "quit". */
static void live_line(char *line) {
    bool down = strncmp(line, "down ", 5) == 0;
    if (down || strncmp(line, "up ", 3) == 0) {
        const char *name = line + (down ? 5 : 3);
        bool on = strcasecmp(name, "ON") == 0;
        int key = on ? PX_KEY_ON : key_by_name(name);
        if (on && !on_is_key) {
            if (down)
                quit_asked = true;
        } else if (key >= 0)
            push_event(down ? key : key | PX_KEY_RELEASED);
    } else if (strncmp(line, "serial ", 7) == 0) {
        for (const char *s = line + 7; *s && serial_in_len < sizeof(serial_in) - 1; s++)
            serial_in[serial_in_len++] = *s;
        serial_in[serial_in_len++] = '\r';
        serial_arrived = true;
    } else if (strcmp(line, "quit") == 0) {
        quit_asked = true;
    }
}

/* Waits up to ms (< 0: as long as it takes) for stdin to say something, and
 * acts on the lines that come. */
static void live_input(int ms) {
    struct pollfd p = {0, POLLIN, 0};
    if (!input_open) {
        if (ms > 0)
            usleep((useconds_t) ms * 1000);
        return;
    }
    if (poll(&p, 1, ms) <= 0)
        return;
    char buf[256];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n <= 0) {
        input_open = false;
        quit_asked = true; /* the bridge has gone */
        return;
    }
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            input_line[input_len] = 0;
            live_line(input_line);
            input_len = 0;
        } else if (input_len < sizeof(input_line) - 1) {
            input_line[input_len++] = buf[i];
        }
    }
}

/* key() with the computer's clock: the next event the page sent, or the wait
 * running out. The end of the app is as in the script: only when it waits. */
static int live_key(int wait_ms) {
    uint32_t start = live_clock();
    for (;;) {
        live_input(0);
        if (event_count > 0) {
            int key = events[event_first];
            event_first = (event_first + 1) % EVENTS_MAX;
            event_count--;
            return key;
        }
        if (serial_arrived) {
            serial_arrived = false;
            return PX_KEY_NONE;
        }
        if (wait_ms == 0)
            return PX_KEY_NONE;
        if (quit_asked)
            return PX_KEY_QUIT;
        int left = -1;
        if (wait_ms > 0) {
            uint32_t passed = live_clock() - start;
            if (passed >= (uint32_t) wait_ms)
                return PX_KEY_NONE;
            left = wait_ms - (int) passed;
        }
        flush_frame();
        live_input(left);
    }
}

static int sim_key(int wait_ms) {
    if (live)
        return live_key(wait_ms);
    if (pending_up >= 0) {
        int key = pending_up;
        pending_up = -1;
        return key | PX_KEY_RELEASED;
    }
    pass_time(wait_ms);
    if (serial_arrived) {
        serial_arrived = false;
        return PX_KEY_NONE;
    }
    /* The end of the script, and QUIT, end the app when it waits for a key
     * (wait_ms != 0), not in a quick look at the keys (0): a program still
     * running finishes what it was doing. */
    if (next_word >= word_count)
        return wait_ms == 0 ? PX_KEY_NONE : PX_KEY_QUIT;
    if (wait_ms == 0 && strcasecmp(words[next_word], "QUIT") == 0)
        return PX_KEY_NONE;
    /* The wait ran out before the next key (a pause, a shot or serial text
     * still to come, at their time). */
    const char *next = words[next_word];
    if (strcasecmp(next, "ON") != 0 && strcasecmp(next, "QUIT") != 0 && key_by_name(next) < 0) {
        long t;
        if (!is_time(next, &t) && strncmp(next, "shot:", 5) != 0 && strncmp(next, "serial:", 7) != 0) {
            fprintf(stderr, "%s: not a key, a time, a shot or serial text\n", next);
            exit(2);
        }
        return PX_KEY_NONE;
    }
    const char *w = words[next_word++];
    if (strcasecmp(w, "QUIT") == 0 || (strcasecmp(w, "ON") == 0 && !on_is_key))
        return PX_KEY_QUIT;
    if (strcasecmp(w, "ON") == 0) {
        pending_up = PX_KEY_ON;
        return PX_KEY_ON;
    }
    int key = key_by_name(w);
    pending_up = key;
    return key;
}

static uint32_t sim_millis(void) {
    if (live)
        return live_clock();
    if (pass_time(1) == 0)
        now_ms++; /* at a key: time moves on all the same */
    return now_ms;
}

static void sim_sleep(int ms) {
    if (live) {
        flush_frame();
        if (ms > 0)
            usleep((useconds_t) ms * 1000);
        return;
    }
    uint32_t passed = pass_time(ms > 0 ? ms : 0);
    if (ms > 0 && passed < (uint32_t) ms)
        now_ms += (uint32_t) ms - passed; /* at a key: it waits for key() */
}

static void sim_datetime(px_datetime_t *now) { /* 2026-10-27 12:00 when the app starts */
    if (live) {
        struct timespec t;
        struct tm tm;
        clock_gettime(CLOCK_REALTIME, &t);
        localtime_r(&t.tv_sec, &tm);
        *now = (px_datetime_t) {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                                tm.tm_min,         tm.tm_sec,     (int32_t) (t.tv_nsec / 1000000), tm.tm_wday};
        return;
    }
    uint32_t s = 12 * 3600 + now_ms / 1000;
    *now = (px_datetime_t) {2026, 10, 27, (int32_t) (s / 3600 % 24), (int32_t) (s / 60 % 60), (int32_t) (s % 60),
                            (int32_t) (now_ms % 1000), 2};
}

static uint32_t sim_random(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

/* ----- sound ----- */

static uint32_t sound_end_ms;
static int live_play(const char *path, bool wait);

/* The file a TONE number is: 0-9 are /SOUNDS/TONES, 10 the error sound. */
static const char *tone_path(int tone, char *buf, size_t size) {
    if (tone == 10)
        return "/SOUNDS/ERROR.wav";
    snprintf(buf, size, "/SOUNDS/TONES/TONE%d.wav", tone);
    return buf;
}

static void sim_tone(int tone) {
    char buf[40];
    if (live)
        live_play(tone_path(tone, buf, sizeof(buf)), true);
    else
        printf("%6u ms  tone %d\n", stamp(), tone);
}

static void sim_tone_async(int tone) {
    char buf[40];
    if (live)
        live_play(tone_path(tone, buf, sizeof(buf)), false);
    else
        printf("%6u ms  tone %d, not waiting\n", stamp(), tone);
}

/* A MIDI song "plays" for SIM_MIDI_MS from midi_play(): the simulator has
 * no synthesizer, only the answers midi_status() would give. */
#define SIM_MIDI_MS 20000
static uint32_t midi_start_ms, midi_end_ms;
static bool full_path(const char *path, char *full, size_t size);

static int sim_midi_play(const char *path) {
    char full[1200];
    if (!full_path(path, full, sizeof(full)))
        return PX_MIDI_NOT_FOUND;
    FILE *f = fopen(full, "rb");
    if (f == NULL) {
        printf("%6u ms  midi %s (not found)\n", stamp(), path);
        return PX_MIDI_NOT_FOUND;
    }
    char magic[4] = {0};
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "MThd", 4) == 0;
    fclose(f);
    printf("%6u ms  midi %s%s\n", stamp(), path, ok ? "" : " (not a MIDI file)");
    if (!ok)
        return PX_MIDI_NOT_MIDI;
    midi_start_ms = stamp();
    midi_end_ms = midi_start_ms + SIM_MIDI_MS;
    return 0;
}

static void sim_midi_status(px_midi_status_t *st) {
    memset(st, 0, sizeof(*st));
    if (stamp() >= midi_end_ms)
        return;
    st->length_ms = SIM_MIDI_MS;
    st->elapsed_ms = stamp() - midi_start_ms;
    st->channels = 0x0201u << (st->elapsed_ms / 300 % 7) | 0x0200u; /* something moving */
    st->voices = (uint32_t) __builtin_popcount(st->channels) * 2;
}

/* In a script sounds take no time: none is ever still playing (a MIDI
 * song is). */
static int sim_sound_playing(void) {
    return (live && stamp() < sound_end_ms) || stamp() < midi_end_ms;
}

static void sim_sound_stop(void) {
    midi_end_ms = 0;
    if (live && sound_end_ms != 0) {
        sound_end_ms = 0;
        emit('X', NULL, 0);
    }
}

/* ----- files: DRIVE_DIR is the drive ----- */

static bool full_path(const char *path, char *full, size_t size) {
    if (strlen(path) > PX_PATH_MAX || strstr(path, "..") != NULL)
        return false;
    if (path[0] == '/')
        snprintf(full, size, "%s%s", drive_dir, path);
    else
        snprintf(full, size, "%s%s%s%s", drive_dir, SIM_APP_DIR, path[0] ? "/" : "", path);
    return true;
}

static void make_dirs(const char *path) { /* every folder in path, as mkdir -p */
    char p[1200];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    mkdir(p, 0755);
}

/* How long a WAV file lasts, in ms: its data chunk over its byte rate. -1 if
 * it isn't one the page could play either. */
static int wav_ms(const char *full) {
    uint8_t h[12], c[8], fmt[16];
    uint32_t byte_rate = 0;
    int ms = -1;
    FILE *f = fopen(full, "rb");
    if (f == NULL)
        return -1;
    if (fread(h, 1, 12, f) == 12 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0) {
        while (fread(c, 1, 8, f) == 8) {
            uint32_t size = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t) c[7] << 24;
            if (memcmp(c, "fmt ", 4) == 0 && size >= 16 && fread(fmt, 1, 16, f) == 16) {
                byte_rate = fmt[8] | fmt[9] << 8 | fmt[10] << 16 | (uint32_t) fmt[11] << 24;
                if (fseek(f, (long) (size - 16 + (size & 1)), SEEK_CUR) != 0)
                    break;
            } else if (memcmp(c, "data", 4) == 0) {
                ms = byte_rate > 0 ? (int) ((uint64_t) size * 1000 / byte_rate) : -1;
                break;
            } else if (fseek(f, (long) (size + (size & 1)), SEEK_CUR) != 0) {
                break;
            }
        }
    }
    fclose(f);
    return ms;
}

/* The page plays the file; the app waits as long as it lasts (wait), or not.
 * A new sound stops the one playing. */
static int live_play(const char *path, bool wait) {
    char full[1200], on_drive[1200];
    int ms;
    if (!full_path(path, full, sizeof(full)) || access(full, R_OK) != 0) {
        printf("%6u ms  play %s (not found)\n", stamp(), path);
        return -1;
    }
    ms = wav_ms(full);
    printf("%6u ms  play %s%s%s\n", stamp(), path, wait ? "" : ", not waiting", ms < 0 ? " (not a WAV file?)" : "");
    snprintf(on_drive, sizeof(on_drive), "%s\n", full + strlen(drive_dir));
    flush_frame();
    emit('S', on_drive, strlen(on_drive));
    sound_end_ms = stamp() + (uint32_t) (ms > 0 ? ms : 0);
    if (wait && ms > 0)
        usleep((useconds_t) ms * 1000);
    return 0;
}

static int sim_play(const char *path) {
    if (live)
        return live_play(path, true);
    char full[1200];
    bool ok = full_path(path, full, sizeof(full)) && access(full, R_OK) == 0;
    printf("%6u ms  play %s%s\n", stamp(), path, ok ? "" : " (not found)");
    return ok ? 0 : -1;
}

static int sim_play_async(const char *path) {
    if (live)
        return live_play(path, false);
    char full[1200];
    bool ok = full_path(path, full, sizeof(full)) && access(full, R_OK) == 0;
    printf("%6u ms  play %s, not waiting%s\n", stamp(), path, ok ? "" : " (not found)");
    return ok ? 0 : -1;
}

static int sim_open(const char *path, const char *mode) {
    char full[1200];
    bool write = strcmp(mode, "w") == 0;
    if ((!write && strcmp(mode, "r") != 0) || !full_path(path, full, sizeof(full)))
        return -1;
    for (int i = 0; i < PX_FILES_OPEN; i++)
        if (files[i] == NULL) {
            if (write) {
                char dir[1200];
                snprintf(dir, sizeof(dir), "%s", full);
                *strrchr(dir, '/') = 0;
                make_dirs(dir);
            }
            files[i] = fopen(full, write ? "wb" : "rb");
            return files[i] != NULL ? i : -1;
        }
    return -1;
}

static int sim_read(int file, void *data, int size) {
    if (file < 0 || file >= PX_FILES_OPEN || files[file] == NULL || size < 0)
        return -1;
    return (int) fread(data, 1, (size_t) size, files[file]);
}

static int sim_write(int file, const void *data, int size) {
    if (file < 0 || file >= PX_FILES_OPEN || files[file] == NULL || size < 0)
        return -1;
    return (int) fwrite(data, 1, (size_t) size, files[file]);
}

static int sim_close(int file) {
    if (file < 0 || file >= PX_FILES_OPEN || files[file] == NULL)
        return -1;
    int r = fclose(files[file]) == 0 ? 0 : -1;
    files[file] = NULL;
    return r;
}

static int sim_load(const char *path, void *data, int size) {
    int f = sim_open(path, "r");
    if (f < 0)
        return -1;
    int n = sim_read(f, data, size);
    sim_close(f);
    return n;
}

static int sim_save(const char *path, const void *data, int size) {
    int f = sim_open(path, "w");
    if (f < 0)
        return -1;
    int ok = sim_write(f, data, size) == size;
    printf("%6u ms  saved %s (%d bytes)\n", stamp(), path, size);
    return sim_close(f) == 0 && ok ? 0 : -1;
}

static int sim_remove(const char *path) {
    char full[1200];
    return full_path(path, full, sizeof(full)) && (remove(full) == 0) ? 0 : -1;
}

static int sim_mkdir(const char *path) {
    char full[1200];
    if (!full_path(path, full, sizeof(full)))
        return -1;
    make_dirs(full);
    return 0;
}

static int compare_names(const void *a, const void *b) {
    return strcasecmp(a, b);
}

static int sim_list(const char *dir, const char *ext, char (*names)[PX_FILE_NAME_MAX + 1], int max) {
    char full[1200];
    if (!full_path(dir, full, sizeof(full)))
        return 0;
    DIR *d = opendir(full);
    int n = 0;
    size_t ext_len = strlen(ext);
    for (struct dirent *e; d != NULL && n < max && (e = readdir(d)) != NULL;) {
        size_t len = strlen(e->d_name);
        if (e->d_name[0] != '.' && len > ext_len && len - ext_len <= PX_FILE_NAME_MAX
            && strcasecmp(e->d_name + len - ext_len, ext) == 0) {
            memcpy(names[n], e->d_name, len - ext_len);
            names[n++][len - ext_len] = 0;
        }
    }
    if (d != NULL)
        closedir(d);
    qsort(names, (size_t) n, sizeof(names[0]), compare_names);
    return n;
}

/* ----- serial, infrared, power ----- */

static int sim_serial_write(const void *data, int size) {
    fwrite(data, 1, (size_t) size, stdout);
    return size;
}

static int sim_serial_read(void *data, int size) {
    int n = 0;
    while (n < size && serial_in_pos < serial_in_len)
        ((char *) data)[n++] = serial_in[serial_in_pos++];
    return n;
}

static void sim_ir_send(const px_ir_burst_t *b) {
    printf("%6u ms  IR %u Hz, %d timings:", stamp(), b->carrier_hz, b->count);
    for (int i = 0; i < b->count && i < 8; i++)
        printf(" %u", b->us[i]);
    printf(b->count > 8 ? " ...\n" : "\n");
}

static int sim_battery_mv(void) {
    return 4000;
}

static int sim_usb_connected(void) {
    return 0;
}

static void sim_on_key(int on) {
    on_is_key = on != 0;
}

static const px_api_t sim_api = {
        .version = PX_APP_API_VERSION,
        .screen = screen,
        .show = sim_show,
        .clear = sim_clear,
        .pixel = sim_pixel,
        .get_pixel = sim_get_pixel,
        .fill = sim_fill,
        .line = sim_line,
        .bitmap = sim_bitmap,
        .text = sim_text,
        .text_width = sim_text_width,
        .font_height = sim_font_height,
        .key = sim_key,
        .millis = sim_millis,
        .sleep = sim_sleep,
        .datetime = sim_datetime,
        .random = sim_random,
        .tone = sim_tone,
        .play = sim_play,
        .tone_async = sim_tone_async,
        .play_async = sim_play_async,
        .sound_playing = sim_sound_playing,
        .sound_stop = sim_sound_stop,
        .load = sim_load,
        .save = sim_save,
        .open = sim_open,
        .read = sim_read,
        .write = sim_write,
        .close = sim_close,
        .remove = sim_remove,
        .mkdir = sim_mkdir,
        .list = sim_list,
        .serial_write = sim_serial_write,
        .serial_read = sim_serial_read,
        .ir_send = sim_ir_send,
        .battery_mv = sim_battery_mv,
        .usb_connected = sim_usb_connected,
        .dec = sim_dec_op,
        .dec_from_int = sim_dec_from_int,
        .dec_to_int = sim_dec_to_int,
        .dec_from_string = sim_dec_from_string,
        .dec_to_string = sim_dec_to_string,
        .dec_cmp = sim_dec_cmp,
        .dec_class = sim_dec_class,
        .on_key = sim_on_key,
        .midi_play = sim_midi_play,
        .midi_status = sim_midi_status,
};

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--live") == 0) {
        live = true;
        drive_dir = argv[2];
        frame_fd = atoi(argv[3]);
        make_dirs(drive_dir);
        signal(SIGPIPE, SIG_IGN);
        setvbuf(stdout, NULL, _IONBF, 0);
        clock_gettime(CLOCK_MONOTONIC, &live_start);
        dprintf(frame_fd, "%d %d %d\n", PX_SCREEN_W, PX_SCREEN_H, PX_SCREEN_STRIDE);
    } else if (argc < 3) {
        fprintf(stderr, "usage: %s OUT_DIR DRIVE_DIR [WORD...]\n       %s --live DRIVE_DIR FRAME_FD\n", argv[0],
                argv[0]);
        return 2;
    } else {
        out_dir = argv[1];
        drive_dir = argv[2];
        make_dirs(out_dir);
        make_dirs(drive_dir);
        words = argv + 3;
        word_count = argc - 3;
    }
    px = &sim_api;
    int result = app_main();
    sim_sound_stop(); /* a sound still playing stops when the app ends */
    flush_frame();
    printf("%6u ms  app_main returned %d%s\n", stamp(), result, result == PX_EXIT_OFF ? " (turn off)" : "");
    return 0;
}
