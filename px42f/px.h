#ifndef PX_H
#define PX_H

/* What an app includes: the firmware's table (px_app.h) as `px`, set
 * before app_main() runs. */
#include <stdbool.h>
#include <stdint.h>

#include "px_app.h"

extern const px_api_t *px;

/* The app: returning ends it (back to Settings -> Games & Apps). It must
 * also return when px->key() says PX_KEY_QUIT. */
int app_main(void);

#endif
