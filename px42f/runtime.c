/* The start of every .bin: its header (px_app.h), the entry the firmware
 * calls, and the heap for malloc(). Not used by the simulator (sim/). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "px.h"

const px_api_t *px;

static int px_start(const px_api_t *api) {
    px = api;
    return app_main();
}

/* image_size, bss_size and crc32 are filled in by pxbin.py after linking. */
__attribute__((section(".px_header"), used)) const px_app_header_t px_app_header = {
        .magic = PX_APP_MAGIC,
        .api_version = PX_APP_API_VERSION,
        .entry = (uint32_t) px_start,
};

/* malloc()'s memory: from the end of the bss (app.ld) to the end of the
 * app's RAM. */
extern char __heap_start[];
static char *heap_top;

void *_sbrk(ptrdiff_t incr) {
    if (heap_top == NULL)
        heap_top = __heap_start;
    if (incr > (char *) (PX_APP_BASE + PX_APP_RAM) - heap_top) {
        errno = ENOMEM;
        return (void *) -1;
    }
    char *prev = heap_top;
    heap_top += incr;
    return prev;
}

/* printf() and the other stdio output go to the USB console; stdin reads
 * what has come in from it (px->serial_read: nothing waits). A terminal
 * counts as one, so output is sent line by line. */
int _write(int fd, const char *data, int size) {
    (void) fd;
    return px->serial_write(data, size);
}

int _read(int fd, char *data, int size) {
    (void) fd;
    return px->serial_read(data, size);
}

int _isatty(int fd) {
    return fd <= 2;
}

/* What else stdio asks of the console: it's a character device, and it
 * can't be closed or seeked. (The drive's files are px->open() etc.) */
int _fstat(int fd, struct stat *st) {
    (void) fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _close(int fd) {
    (void) fd;
    return -1;
}

int _lseek(int fd, int offset, int whence) {
    (void) fd;
    (void) offset;
    (void) whence;
    return -1;
}

/* raise() (after a division by zero in the C library's 64-bit division):
 * no signals here, nothing to kill. */
int _kill(int pid, int sig) {
    (void) pid;
    (void) sig;
    return -1;
}

int _getpid(void) {
    return 1;
}
