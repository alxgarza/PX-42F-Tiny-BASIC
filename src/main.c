/* PX-BASIC, the PX-42F's BASIC (based on Tiny BASIC): the prompt, and the
 * machine the interpreter (basic.c) runs on -- bio.h, on the console
 * (console.c) and the PX-42F app framework. Programs are .BAS text files in the app's folder,
 * /BIN/APPS/BASIC/; the one being written is kept in AUTOSAVE.BAS when
 * the app ends, and comes back when it starts. */
#include <stdio.h>
#include <string.h>

#include "basic.h"
#include "bio.h"
#include "console.h"
#include "px.h"

#define AUTOSAVE "AUTOSAVE"

/* ===================== bio.h ===================== */

void bio_out(const char *s, int n) {
    console_out(s, n);
}

int bio_column(void) {
    return console_column();
}

int bio_input(char *buf, int size) {
    return console_input(buf, size, true);
}

bool bio_stop(void) {
    return console_poll();
}

int bio_key(void) {
    return console_key();
}

void bio_help(void) {
    console_help();
}

void bio_exit(void) {
    console_exit();
}

int bio_inkey(void) {
    return console_inkey();
}

void bio_cls(void) {
    console_cls();
}

void bio_at(int col, int row) {
    console_at(col, row);
}

void bio_pset(int x, int y, int black) {
    px->pixel(x, y, black);
    console_drawn();
}

void bio_line(int x0, int y0, int x1, int y1, int black) {
    px->line(x0, y0, x1, y1, black);
    console_drawn();
}

void bio_box(int x, int y, int w, int h, int black) {
    px->fill(x, y, w, h, black);
    console_drawn();
}

void bio_tone(int tone) {
    console_flush(); /* what was printed before it, on the screen as it plays */
    px->tone(tone);
}

int bio_midi_play(const char *name) {
    char path[PX_PATH_MAX];
    console_flush();
    snprintf(path, sizeof(path), "/MIDI/%s.mid", name);
    int r = px->midi_play(path);
    return r == 0 ? 0 : r == PX_MIDI_NOT_FOUND || r == PX_MIDI_NOT_MIDI ? BIO_MIDI_MISSING : BIO_MIDI_TOO_BIG;
}

bool bio_wav_play(const char *name) {
    char path[PX_PATH_MAX];
    console_flush();
    snprintf(path, sizeof(path), "/SOUNDS/%s.wav", name);
    return px->play_async(path) == 0;
}

void bio_midi_stop(void) {
    px->sound_stop();
}

bool bio_midi_playing(void) {
    return px->sound_playing() != 0;
}

bool bio_wait(int ms) {
    return console_wait(ms);
}

uint32_t bio_random(void) {
    return px->random();
}

static void file_path(char *path, size_t size, const char *name) {
    snprintf(path, size, "%s.BAS", name);
}

int bio_open(const char *name, bool write) {
    char path[BIO_NAME_MAX + 8];
    file_path(path, sizeof(path), name);
    return px->open(path, write ? "w" : "r");
}

int bio_read(int file, char *buf, int n) {
    return px->read(file, buf, n);
}

int bio_write(int file, const char *buf, int n) {
    return px->write(file, buf, n);
}

bool bio_close(int file) {
    return px->close(file) == 0;
}

bool bio_remove(const char *name) {
    char path[BIO_NAME_MAX + 8];
    file_path(path, sizeof(path), name);
    return px->remove(path) == 0;
}

int bio_files(char (*names)[BIO_NAME_MAX + 1], int max) {
    static char all[64][PX_FILE_NAME_MAX + 1];
    int count = px->list("", ".BAS", all, max < 64 ? max : 64), n = 0;
    for (int i = 0; i < count; i++)
        if (strlen(all[i]) <= BIO_NAME_MAX && strcmp(all[i], AUTOSAVE) != 0)
            snprintf(names[n++], BIO_NAME_MAX + 1, "%s", all[i]);
    return n;
}

/* ===================== the prompt ===================== */

static bool exists(const char *name) {
    int f = bio_open(name, false);
    if (f >= 0)
        bio_close(f);
    return f >= 0;
}

int app_main(void) {
    px->on_key(1); /* ON asks before leaving (console.c) */
    console_init();
    basic_new();
    if (exists(AUTOSAVE))
        basic_enter("LOAD \"" AUTOSAVE "\"");
    basic_saved();
    char line[BASIC_LINE_MAX + 1];
    snprintf(line, sizeof(line), "PX-BASIC\n%d BYTES FREE\n", basic_free());
    console_out(line, (int) strlen(line));

    while (!console_quit()) {
        console_out(">", 1);
        if (console_input(line, sizeof(line), false) < 0)
            break;
        console_running(true);
        basic_enter(line);
        console_running(false);
    }

    /* The program as it is, for next time. */
    if (basic_changed())
        basic_enter("SAVE \"" AUTOSAVE "\"");
    return PX_EXIT_MENU;
}
