/* Optional parts of ext-buffers that are not in the core (--enable-buffers-extras); see docs/extras.md. */
#ifndef BUFFERS_EXTRAS_H
#define BUFFERS_EXTRAS_H

/* registers Buffers\\Compiled; called from MINIT after the core classes are registered */
void buf_extras_minit(void);
/* frees the compiled code of the request; called from RSHUTDOWN */
void buf_extras_rshutdown(void);

#endif
