/* The native backend (native.c): CIR compiled to machine code with IR, on the CPUs IR targets (x86-64, AArch64). Elsewhere,
 * and in byte-swapping builds, CIR runs in the interpreter (cir.c). */
#ifndef BUF_CC_NATIVE_H
#define BUF_CC_NATIVE_H

#include "front.h"

#if (defined(IR_TARGET_X64) || defined(IR_TARGET_AARCH64)) && !BUF_SWAP
# define BUF_CC_NATIVE 1
/* compiles the kernel's code into k->native; false if it cannot (the kernel is then interpreted) */
bool cc_native_compile(cc_kernel *k);
void cc_native_run(const cc_kernel *k, cc_ctx *ctx);
#endif

#endif
