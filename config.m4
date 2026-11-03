PHP_ARG_ENABLE([buffers],
  [whether to enable buffers support],
  [AS_HELP_STRING([--enable-buffers],
    [Enable buffers support])],
  [no])

if test "$PHP_BUFFERS" != "no"; then
  AC_DEFINE(HAVE_BUFFERS, 1, [ Have buffers support ])
  PHP_NEW_EXTENSION(buffers, buffers.c dtype.c device.c shape.c buffer.c props.c view.c capsule.c transfer.c convert.c api.c, $ext_shared)
  PHP_INSTALL_HEADERS([ext/buffers], [php_buffers_api.h dlpack.h])
fi
