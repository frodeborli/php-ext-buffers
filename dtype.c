#include "php_dlpack.h"
#include "codec.h"
#include "zend_enum.h"
#include "dlpack_arginfo.h"

zend_class_entry *dlp_ce_dtype;

const dlp_dtype_info dlp_dtypes[DLP_NTYPES] = {
#define X(id, name, value, kind, bits, size, code) { #name, value, kind, bits, size, code },
	DLP_DTYPES(X)
#undef X
};

/* decode tables for every format of at most 8 bits, indexed by dtype - DLP_T_F8E3M4 */
double dlp_tab[DLP_T_F4E2M1FN - DLP_T_F8E3M4 + 1][256];

static HashTable dlp_by_value; /* backing string -> dtype index + 1, persistent */

void dlp_codec_init(void)
{
	for (int c = 0; c < 256; c++) {
#define X(id, E, M, B, K) dlp_tab[DLP_T_##id - DLP_T_F8E3M4][c] = dlp_dec_generic(c, E, M, B, K);
		DLP_SMALL(X)
#undef X
		dlp_tab[DLP_T_F8E8M0FNU - DLP_T_F8E3M4][c] = dlp_dec_e8m0(c);
	}
}

void dlp_dtype_minit(void)
{
	zval v;

	dlp_codec_init();
	zend_hash_init(&dlp_by_value, DLP_NTYPES, NULL, NULL, 1);
	dlp_ce_dtype = zend_register_internal_enum("DType", IS_STRING, class_DType_methods);
	for (int i = 0; i < DLP_NTYPES; i++) {
		ZVAL_STR(&v, zend_string_init_interned(dlp_dtypes[i].value, strlen(dlp_dtypes[i].value), 1));
		zend_enum_add_case_cstr(dlp_ce_dtype, dlp_dtypes[i].name, &v);
		ZVAL_LONG(&v, i + 1);
		zend_hash_str_add(&dlp_by_value, dlp_dtypes[i].value, strlen(dlp_dtypes[i].value), &v);
	}
}

void dlp_dtype_mshutdown(void)
{
	zend_hash_destroy(&dlp_by_value);
}

int dlp_dtype_from_object(zend_object *o)
{
	zval *backing = zend_enum_fetch_case_value(o);
	zval *idx = zend_hash_find(&dlp_by_value, Z_STR_P(backing));

	return Z_LVAL_P(idx) - 1;
}

void dlp_dtype_to_zval(int dt, zval *rv)
{
	ZVAL_OBJ_COPY(rv, zend_enum_get_case_cstr(dlp_ce_dtype, dlp_dtypes[dt].name));
}

#define SELF (&dlp_dtypes[dlp_dtype_from_object(Z_OBJ_P(ZEND_THIS))])

PHP_METHOD(DType, bits) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->bits); }
PHP_METHOD(DType, byteSize) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->size); }
PHP_METHOD(DType, isByteAligned) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_TRUE; }
PHP_METHOD(DType, isInteger) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->kind == DLP_INT || SELF->kind == DLP_UINT); }
PHP_METHOD(DType, isFloatingPoint) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->kind == DLP_FLOAT); }
PHP_METHOD(DType, isComplex) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(SELF->kind == DLP_COMPLEX); }

PHP_METHOD(DType, dlpack)
{
	const dlp_dtype_info *d = SELF;

	ZEND_PARSE_PARAMETERS_NONE();
	array_init_size(return_value, 3);
	add_assoc_long(return_value, "code", d->code);
	add_assoc_long(return_value, "bits", d->bits);
	add_assoc_long(return_value, "lanes", 1);
}
