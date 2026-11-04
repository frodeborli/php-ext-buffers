PHP_ARG_ENABLE([buffers],
  [whether to enable buffers support],
  [AS_HELP_STRING([--enable-buffers],
    [Enable buffers support])],
  [no])

PHP_ARG_WITH([buffers-gd],
  [for Tensor::fromGdImage() and toGdImage()],
  [AS_HELP_STRING([--with-buffers-gd@<:@=DIR@:>@],
    [Build the GD integration when gd.h is found (DIR: where to look first; default: the PHP include dir, /usr, /usr/local). ext/gd itself is only needed at run time])],
  [yes], [no])

if test "$PHP_BUFFERS" != "no"; then
  AC_DEFINE(HAVE_BUFFERS, 1, [ Have buffers support ])
  buffers_sources="buffers.c dtype.c device.c shape.c buffer.c props.c view.c capsule.c transfer.c convert.c api.c"

  if test "$PHP_BUFFERS_GD" != "no"; then
    AC_MSG_CHECKING([for gd.h])
    buffers_gd_inc=
    for d in "$PHP_BUFFERS_GD/include" "$PHP_BUFFERS_GD" "`$PHP_CONFIG --include-dir`/ext/gd/libgd" /usr/include /usr/local/include; do
      if test -r "$d/gd.h"; then buffers_gd_inc=$d; break; fi
    done
    if test -n "$buffers_gd_inc"; then
      AC_MSG_RESULT([$buffers_gd_inc/gd.h])
      AC_DEFINE(HAVE_BUFFERS_GD, 1, [ Build the GD integration ])
      PHP_ADD_INCLUDE([$buffers_gd_inc])
      buffers_sources="$buffers_sources gd.c"
    else
      AC_MSG_RESULT([not found, building without the GD integration])
    fi
  fi

  PHP_NEW_EXTENSION(buffers, $buffers_sources, $ext_shared)
  PHP_INSTALL_HEADERS([ext/buffers], [php_buffers_api.h dlpack.h])
fi
