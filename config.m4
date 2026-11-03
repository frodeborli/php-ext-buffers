PHP_ARG_ENABLE([buffers],
  [whether to enable buffers support],
  [AS_HELP_STRING([--enable-buffers],
    [Enable buffers support])],
  [no])

PHP_ARG_ENABLE([buffers-testing],
  [whether to build the DLPack test helpers],
  [AS_HELP_STRING([--enable-buffers-testing],
    [Add test-only functions that produce foreign DLPack tensors (for the test suite)])],
  [no],
  [no])

if test "$PHP_BUFFERS" != "no"; then
  if test "$PHP_BUFFERS_TESTING" != "no"; then
    AC_DEFINE([BUFFERS_TESTING], [1], [Build the test helpers])
  fi
  AC_DEFINE(HAVE_BUFFERS, 1, [ Have buffers support ])
  PHP_NEW_EXTENSION(buffers, buffers.c dtype.c device.c shape.c buffer.c props.c view.c capsule.c api.c, $ext_shared)
  PHP_INSTALL_HEADERS([ext/buffers], [php_buffers_api.h dlpack.h])
fi
