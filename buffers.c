#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_buffers.h"
#include "ext/standard/info.h"

ZEND_DECLARE_MODULE_GLOBALS(buffers)

zend_class_entry *buf_json_serializable(void)
{
	return zend_hash_str_find_ptr(CG(class_table), "jsonserializable", sizeof("jsonserializable") - 1);
}

int buf_compare_identity(zval *a, zval *b)
{
	ZEND_COMPARE_OBJECTS_FALLBACK(a, b);
	return Z_OBJ_P(a) == Z_OBJ_P(b) ? 0 : ZEND_UNCOMPARABLE;
}

static PHP_GINIT_FUNCTION(buffers)
{
#if defined(ZTS) && defined(COMPILE_DL_BUFFERS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	memset(buffers_globals, 0, sizeof(*buffers_globals));
}

PHP_MINIT_FUNCTION(buffers)
{
	buf_dtype_minit();
	buf_device_minit();
	buf_shape_minit();
	buf_buffer_minit();
	buf_view_minit();
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(buffers)
{
	buf_dtype_mshutdown();
	buf_props_mshutdown();
	return SUCCESS;
}

PHP_RINIT_FUNCTION(buffers)
{
#if defined(ZTS) && defined(COMPILE_DL_BUFFERS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	BUFG(request_active) = true;
	return SUCCESS;
}

/* Parked sub-views are not freed by the engine: release them here, before the object store goes. */
PHP_RSHUTDOWN_FUNCTION(buffers)
{
	buf_view_rshutdown();
	buf_device_rshutdown();
	BUFG(request_active) = false;
	return SUCCESS;
}

PHP_MINFO_FUNCTION(buffers)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "buffers support", "enabled");
	php_info_print_table_row(2, "extension version", PHP_BUFFERS_VERSION);
	php_info_print_table_row(2, "DLPack version", "1.3");
	php_info_print_table_row(2, "C API version", ZEND_TOSTR(BUFFERS_API_VERSION));
	php_info_print_table_end();
}

static const zend_module_dep buffers_deps[] = {
	ZEND_MOD_OPTIONAL("json")   /* Tensor and Shape implement JsonSerializable when it is there */
	ZEND_MOD_OPTIONAL("gd")     /* loaded first when present: Tensor::fromGdImage() and toGdImage() need it at run time */
	ZEND_MOD_END
};

zend_module_entry buffers_module_entry = {
	STANDARD_MODULE_HEADER_EX, NULL, buffers_deps,
	"buffers",
	NULL,
	PHP_MINIT(buffers),
	PHP_MSHUTDOWN(buffers),
	PHP_RINIT(buffers),
	PHP_RSHUTDOWN(buffers),
	PHP_MINFO(buffers),
	PHP_BUFFERS_VERSION,
	PHP_MODULE_GLOBALS(buffers),
	PHP_GINIT(buffers),
	NULL,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_BUFFERS
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(buffers)
#endif
