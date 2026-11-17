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

PHP_ARG_ENABLE([buffers-extras],
  [whether to build the ext-buffers extras],
  [AS_HELP_STRING([--disable-buffers-extras],
    [Build without the optional extras (Buffers\\Compiled; docs/extras.md)])],
  [yes], [no])

if test "$PHP_BUFFERS" != "no"; then
  AC_DEFINE(HAVE_BUFFERS, 1, [ Have buffers support ])
  buffers_sources="buffers.c arith.c dtype.c device.c shape.c buffer.c props.c view.c capsule.c transfer.c convert.c api.c dataview.c"

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

  if test "$PHP_BUFFERS_EXTRAS" != "no"; then
    AC_DEFINE(HAVE_BUFFERS_EXTRAS, 1, [ Build the ext-buffers extras ])
    buffers_sources="$buffers_sources extras/compiled.c extras/compiler/cir.c extras/compiler/front.c extras/compiler/native.c"
    dnl the native backend: IR (extras/ir), on the CPUs it targets, as PHP's opcache builds it
    BUFFERS_IR=yes
    AS_CASE([$host_cpu],
      [x86_64*|amd64*], [IR_TARGET=IR_TARGET_X64; DASM_FLAGS="-D X64=1"; DASM_ARCH=x86],
      [aarch64*|arm64*], [IR_TARGET=IR_TARGET_AARCH64; DASM_FLAGS=""; DASM_ARCH=aarch64],
      [BUFFERS_IR=no])
    AS_CASE([$host_os], [darwin*], [AS_VAR_IF([DASM_ARCH], [x86], [DASM_FLAGS="-D X64APPLE=1 -D X64=1"])])
    if test "$BUFFERS_IR" = "yes"; then
      buffers_sources="$buffers_sources extras/ir/ir.c extras/ir/ir_strtab.c extras/ir/ir_cfg.c extras/ir/ir_sccp.c extras/ir/ir_gcm.c extras/ir/ir_ra.c extras/ir/ir_ra_fast.c extras/ir/ir_emit.c extras/ir/ir_check.c extras/ir/ir_cpuinfo.c extras/ir/ir_mem2ssa.c extras/ir/ir_dump.c extras/ir/ir_save.c"
      buffers_ir_cflags="-D$IR_TARGET -I@ext_srcdir@/extras/ir -I@ext_builddir@/extras/ir"
      PHP_SUBST([IR_TARGET])
      PHP_SUBST([DASM_FLAGS])
      PHP_SUBST([DASM_ARCH])
    fi
  fi

  dnl Tensor arithmetic is exact (docs/proposal-arithmetic.md): no contraction of a * b + c into an FMA, which rounds once
  dnl instead of twice and would make results depend on the CPU. GCC's default in GNU C mode is -ffp-contract=fast.
  dnl The element-wise kernels are plain loops: GCC at -O2 only vectorizes loops whose trip count it knows, so ask for its
  dnl normal cost model (clang vectorizes them at -O2 already).
  buffers_cflags=
  AX_CHECK_COMPILE_FLAG([-ffp-contract=off], [buffers_cflags="$buffers_cflags -ffp-contract=off"], , [-Werror])
  dnl Nothing reads errno after a math function; without this GCC calls sqrtf() for errno instead of vectorizing it (the
  dnl results are the same, IEEE square root is correctly rounded either way).
  AX_CHECK_COMPILE_FLAG([-fno-math-errno], [buffers_cflags="$buffers_cflags -fno-math-errno"], , [-Werror])
  AX_CHECK_COMPILE_FLAG([-fvect-cost-model=dynamic], [buffers_cflags="$buffers_cflags -fvect-cost-model=dynamic"], , [-Werror])
  buffers_cflags="$buffers_cflags $buffers_ir_cflags"
  PHP_NEW_EXTENSION(buffers, $buffers_sources, $ext_shared,, $buffers_cflags)
  PHP_ADD_BUILD_DIR([$ext_builddir/extras])
  PHP_ADD_BUILD_DIR([$ext_builddir/extras/compiler])
  PHP_ADD_BUILD_DIR([$ext_builddir/extras/ir])
  if test "$PHP_BUFFERS_EXTRAS" != "no" && test "$BUFFERS_IR" = "yes"; then
    PHP_ADD_MAKEFILE_FRAGMENT([$ext_srcdir/extras/ir/Makefile.frag])
  fi
  PHP_INSTALL_HEADERS([ext/buffers], [php_buffers_api.h dlpack.h])
fi
