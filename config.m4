PHP_ARG_ENABLE([dlpack],
  [whether to enable dlpack support],
  [AS_HELP_STRING([--enable-dlpack],
    [Enable dlpack support])],
  [no])

PHP_ARG_ENABLE([dlpack-testing],
  [whether to build the DLPack test helpers],
  [AS_HELP_STRING([--enable-dlpack-testing],
    [Add test-only functions that produce foreign DLPack tensors (for the test suite)])],
  [no],
  [no])

if test "$PHP_DLPACK" != "no"; then
  if test "$PHP_DLPACK_TESTING" != "no"; then
    AC_DEFINE([DLPACK_TESTING], [1], [Build the test helpers])
  fi
  AC_DEFINE(HAVE_DLPACK, 1, [ Have dlpack support ])
  PHP_NEW_EXTENSION(dlpack, dlpack.c dtype.c buffer.c view.c capsule.c, $ext_shared)
fi
