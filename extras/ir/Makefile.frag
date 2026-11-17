# The generated parts of IR (as PHP's ext/opcache/jit/Makefile.frag makes them)
$(builddir)/extras/ir/minilua: $(srcdir)/extras/ir/dynasm/minilua.c
	$(BUILD_CC) $(srcdir)/extras/ir/dynasm/minilua.c -lm -o $@

$(builddir)/extras/ir/ir_emit_$(DASM_ARCH).h: $(srcdir)/extras/ir/ir_$(DASM_ARCH).dasc $(srcdir)/extras/ir/dynasm/*.lua $(builddir)/extras/ir/minilua
	$(builddir)/extras/ir/minilua $(srcdir)/extras/ir/dynasm/dynasm.lua $(DASM_FLAGS) -o $@ $(srcdir)/extras/ir/ir_$(DASM_ARCH).dasc

# (make takes extras/ir/x.lo and ./extras/ir/x.lo for different targets: both are named)
$(builddir)/extras/ir/ir_emit.lo extras/ir/ir_emit.lo: $(srcdir)/extras/ir/ir_emit.c $(builddir)/extras/ir/ir_emit_$(DASM_ARCH).h

$(builddir)/extras/ir/gen_ir_fold_hash: $(srcdir)/extras/ir/gen_ir_fold_hash.c $(srcdir)/extras/ir/ir_strtab.c
	$(BUILD_CC) -D$(IR_TARGET) -I$(srcdir)/extras/ir -o $@ $(srcdir)/extras/ir/gen_ir_fold_hash.c

$(builddir)/extras/ir/ir_fold_hash.h: $(builddir)/extras/ir/gen_ir_fold_hash $(srcdir)/extras/ir/ir_fold.h $(srcdir)/extras/ir/ir.h
	$(builddir)/extras/ir/gen_ir_fold_hash < $(srcdir)/extras/ir/ir_fold.h > $@

$(builddir)/extras/ir/ir.lo extras/ir/ir.lo: $(builddir)/extras/ir/ir_fold_hash.h
