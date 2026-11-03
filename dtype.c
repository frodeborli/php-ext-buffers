#include "php_buffers.h"
#include "codec.h"
#include "zend_enum.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_dtype;

const buf_dtype_info buf_dtypes[BUF_NTYPES] = {
#define X(id, name, value, kind, bits, size, code) { #name, value, kind, bits, size, code },
	BUF_DTYPES(X)
#undef X
};

/* decode tables for every format of at most 8 bits, indexed by dtype - BUF_T_F8E3M4 */
double buf_tab[BUF_T_F4E2M1FN - BUF_T_F8E3M4 + 1][256];

static HashTable buf_by_value; /* backing string -> dtype index + 1, persistent */

void buf_codec_init(void)
{
	for (int c = 0; c < 256; c++) {
#define X(id, E, M, B, K) buf_tab[BUF_T_##id - BUF_T_F8E3M4][c] = buf_dec_generic(c, E, M, B, K);
		BUF_SMALL(X)
#undef X
		buf_tab[BUF_T_F8E8M0FNU - BUF_T_F8E3M4][c] = buf_dec_e8m0(c);
	}
}

void buf_dtype_minit(void)
{
	zval v;

	buf_codec_init();
	zend_hash_init(&buf_by_value, BUF_NTYPES, NULL, NULL, 1);
	buf_ce_dtype = zend_register_internal_enum("Buffers\\DType", IS_STRING, class_Buffers_DType_methods);
	for (int i = 0; i < BUF_NTYPES; i++) {
		ZVAL_STR(&v, zend_string_init_interned(buf_dtypes[i].value, strlen(buf_dtypes[i].value), 1));
		zend_enum_add_case_cstr(buf_ce_dtype, buf_dtypes[i].name, &v);
		ZVAL_LONG(&v, i + 1);
		zend_hash_str_add(&buf_by_value, buf_dtypes[i].value, strlen(buf_dtypes[i].value), &v);
	}
}

void buf_dtype_mshutdown(void)
{
	zend_hash_destroy(&buf_by_value);
}

int buf_dtype_from_object(zend_object *o)
{
	zval *backing = zend_enum_fetch_case_value(o);
	zval *idx = zend_hash_find(&buf_by_value, Z_STR_P(backing));

	return Z_LVAL_P(idx) - 1;
}

void buf_dtype_to_zval(int dt, zval *rv)
{
	ZVAL_OBJ_COPY(rv, zend_enum_get_case_cstr(buf_ce_dtype, buf_dtypes[dt].name));
}

#define SELF (&buf_dtypes[buf_dtype_from_object(Z_OBJ_P(ZEND_THIS))])

PHP_METHOD(Buffers_DType, bits) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->bits); }
PHP_METHOD(Buffers_DType, byteSize) { ZEND_PARSE_PARAMETERS_NONE(); if (!SELF->size) RETURN_NULL(); RETURN_LONG(SELF->size); }
PHP_METHOD(Buffers_DType, isByteAligned) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->size != 0); }
PHP_METHOD(Buffers_DType, isInteger) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->kind == BUF_INT || SELF->kind == BUF_UINT); }
PHP_METHOD(Buffers_DType, isFloatingPoint) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->kind == BUF_FLOAT); }
PHP_METHOD(Buffers_DType, isComplex) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->kind == BUF_COMPLEX); }
