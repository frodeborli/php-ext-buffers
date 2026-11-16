/* Optional parts of ext-buffers that are not in the core (--enable-buffers-extras); see docs/extras.md. */
#ifndef BUFFERS_EXTRAS_H
#define BUFFERS_EXTRAS_H

/* adds Tensor::each() and Tensor::assign(); called from MINIT after the core classes are registered */
void buf_extras_minit(void);

#endif
