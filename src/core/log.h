#ifndef _DIME_LOG_H
#define _DIME_LOG_H 

#include <glib.h>
#include "config.h"

#if defined(DIME_DEBUG)
#define dime_info(fmt, ...) g_info("%s: " fmt, __func__, ##__VA_ARGS__)
#define dime_fatal(fmt, ...) g_fatal("%s: " fmt, __func__, ##__VA_ARGS__)
#define dime_warn(fmt, ...) g_warning("%s: " fmt, __func__, ##__VA_ARGS__)
#define dime_debug(fmt, ...) do { \
    /* G_DEBUG_HERE(); */ \
    g_debug("%s: " fmt, __func__, ##__VA_ARGS__); \
} while (0)

#else

#define dime_info(...) do {} while(0)
#define dime_warn(...) do {} while(0)
#define dime_fatal(...) do {} while(0)
#define dime_debug(...) do {} while(0)

#endif /* ~ DIME_DEBUG */

#endif /* ifndef _DIME_LOG_H */
