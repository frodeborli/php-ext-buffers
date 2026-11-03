#include "php_dlpack.h"
#include "zend_interfaces.h"
#include "dlpack_arginfo.h"

zend_class_entry *dlp_ce_buffer_if, *dlp_ce_arraybuffer, *dlp_ce_dlpackbuffer;
static zend_object_handlers arraybuffer_handlers, foreign_handlers;

static zend_object *buffer_create(zend_class_entry *ce, zend_object_handlers *h)
{
	dlp_buffer *b = zend_object_alloc(sizeof(dlp_buffer), ce);

	zend_object_std_init(&b->std, ce);
	b->std.handlers = h;
	return &b->std;
}

static zend_object *arraybuffer_create(zend_class_entry *ce) { return buffer_create(ce, &arraybuffer_handlers); }
static zend_object *foreign_create(zend_class_entry *ce) { return buffer_create(ce, &foreign_handlers); }

static void buffer_free(zend_object *o)
{
	dlp_buffer *b = dlp_buffer_from(o);

	if (b->managed && b->managed->deleter) {
		b->managed->deleter(b->managed);
	}
	if (b->alloc) {
		efree(b->alloc);
	}
	zend_object_std_dtor(o);
}

static zend_function *foreign_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "DLPackBuffer cannot be instantiated; use TypedView::fromDLPack()");
	return NULL;
}

/* A buffer over memory owned by a foreign DLPack tensor; its deleter runs exactly once, when this object is freed. */
zend_object *dlp_foreign_buffer_new(DLManagedTensorVersioned *t, uint8_t *base, size_t len)
{
	zend_object *o = foreign_create(dlp_ce_dlpackbuffer);
	dlp_buffer *b = dlp_buffer_from(o);

	b->managed = t;
	b->base = base;
	b->len = len;
	b->writable = !(t->flags & DLPACK_FLAG_BITMASK_READ_ONLY);
	return o;
}

#define BUF dlp_buffer_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(ArrayBuffer, __construct)
{
	zend_long len;
	dlp_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(len)
	ZEND_PARSE_PARAMETERS_END();

	if (b->base) {
		zend_throw_error(NULL, "ArrayBuffer is already constructed");
		RETURN_THROWS();
	}
	if (len < 0) {
		zend_argument_value_error(1, "must be greater than or equal to 0");
		RETURN_THROWS();
	}
	if (len > ZEND_LONG_MAX / 2) {
		zend_argument_value_error(1, "is too large");
		RETURN_THROWS();
	}
	b->alloc = ecalloc(1, (size_t)len + DLP_ALIGN);
	b->base = (uint8_t *)(((uintptr_t)b->alloc + DLP_ALIGN - 1) & ~(uintptr_t)(DLP_ALIGN - 1));
	b->len = (size_t)len;
	b->writable = true;
}

PHP_METHOD(ArrayBuffer, fromString)
{
	zend_string *s;
	dlp_buffer *b;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(s)
	ZEND_PARSE_PARAMETERS_END();

	object_init_ex(return_value, dlp_ce_arraybuffer);
	b = dlp_buffer_from(Z_OBJ_P(return_value));
	b->alloc = ecalloc(1, ZSTR_LEN(s) + DLP_ALIGN);
	b->base = (uint8_t *)(((uintptr_t)b->alloc + DLP_ALIGN - 1) & ~(uintptr_t)(DLP_ALIGN - 1));
	b->len = ZSTR_LEN(s);
	b->writable = true;
	memcpy(b->base, ZSTR_VAL(s), ZSTR_LEN(s));
}

PHP_METHOD(ArrayBuffer, toString)
{
	dlp_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_STRINGL((const char *)b->base, b->len);
}

#define COMMON(cls) \
	PHP_METHOD(cls, byteLength) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG((zend_long)BUF->len); } \
	PHP_METHOD(cls, isWritable) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(BUF->writable); } \
	PHP_METHOD(cls, view) { dlp_buffer_view_impl(Z_OBJ_P(ZEND_THIS), INTERNAL_FUNCTION_PARAM_PASSTHRU); }
COMMON(ArrayBuffer)
COMMON(DLPackBuffer)

static zend_class_entry *register_buffer(const char *name, const zend_function_entry *methods,
	zend_object *(*create)(zend_class_entry *), zend_object_handlers *h)
{
	zend_class_entry ce, *c;

	INIT_CLASS_ENTRY_EX(ce, name, strlen(name), methods);
	c = zend_register_internal_class_ex(&ce, NULL);
	c->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	c->create_object = create;
	zend_class_implements(c, 1, dlp_ce_buffer_if);
	memcpy(h, &std_object_handlers, sizeof(zend_object_handlers));
	h->offset = XtOffsetOf(dlp_buffer, std);
	h->free_obj = buffer_free;
	h->clone_obj = NULL;
	h->compare = dlp_compare_identity;
	return c;
}

void dlp_buffer_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "BufferInterface", class_BufferInterface_methods);
	dlp_ce_buffer_if = zend_register_internal_interface(&ce);
	dlp_ce_arraybuffer = register_buffer("ArrayBuffer", class_ArrayBuffer_methods, arraybuffer_create, &arraybuffer_handlers);
	dlp_ce_dlpackbuffer = register_buffer("DLPackBuffer", class_DLPackBuffer_methods, foreign_create, &foreign_handlers);
	foreign_handlers.get_constructor = foreign_no_constructor;
}
