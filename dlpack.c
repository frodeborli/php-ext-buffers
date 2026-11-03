#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_dlpack.h"
#include "ext/standard/info.h"

ZEND_DECLARE_MODULE_GLOBALS(dlpack)

#ifdef DLPACK_TESTING
extern const zend_function_entry dlp_testing_functions[];
#define DLP_FUNCTIONS dlp_testing_functions
#else
#define DLP_FUNCTIONS NULL
#endif

int dlp_compare_identity(zval *a, zval *b)
{
	ZEND_COMPARE_OBJECTS_FALLBACK(a, b);
	return Z_OBJ_P(a) == Z_OBJ_P(b) ? 0 : ZEND_UNCOMPARABLE;
}

static PHP_GINIT_FUNCTION(dlpack)
{
#if defined(ZTS) && defined(COMPILE_DL_DLPACK)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	memset(dlpack_globals, 0, sizeof(*dlpack_globals));
}

PHP_MINIT_FUNCTION(dlpack)
{
	dlp_dtype_minit();
	dlp_buffer_minit();
	dlp_view_minit();
	dlp_capsule_minit();
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(dlpack)
{
	dlp_dtype_mshutdown();
	return SUCCESS;
}

PHP_RINIT_FUNCTION(dlpack)
{
#if defined(ZTS) && defined(COMPILE_DL_DLPACK)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	DLPG(request_active) = true;
	return SUCCESS;
}

/* Parked sub-views are not freed by the engine: release them here, before the object store goes. */
PHP_RSHUTDOWN_FUNCTION(dlpack)
{
	dlp_view_rshutdown();
	DLPG(request_active) = false;
	return SUCCESS;
}

PHP_MINFO_FUNCTION(dlpack)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "dlpack support", "enabled");
	php_info_print_table_row(2, "extension version", PHP_DLPACK_VERSION);
	php_info_print_table_row(2, "DLPack version", "1.3");
	php_info_print_table_end();
}

zend_module_entry dlpack_module_entry = {
	STANDARD_MODULE_HEADER,
	"dlpack",
	DLP_FUNCTIONS,
	PHP_MINIT(dlpack),
	PHP_MSHUTDOWN(dlpack),
	PHP_RINIT(dlpack),
	PHP_RSHUTDOWN(dlpack),
	PHP_MINFO(dlpack),
	PHP_DLPACK_VERSION,
	PHP_MODULE_GLOBALS(dlpack),
	PHP_GINIT(dlpack),
	NULL,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_DLPACK
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(dlpack)
#endif
