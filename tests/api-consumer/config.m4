PHP_ARG_ENABLE([buffers-api-test],
  [whether to build the ext-buffers API consumer test extension],
  [AS_HELP_STRING([--enable-buffers-api-test],
    [Build buffers_api_test, an extension that uses only php_buffers_api.h (for the test suite)])],
  [no])

if test "$PHP_BUFFERS_API_TEST" != "no"; then
  PHP_NEW_EXTENSION(buffers_api_test, buffers_api_test.c, $ext_shared)
fi
