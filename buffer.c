#include "php_buffers.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_buffer_if, *buf_ce_arraybuffer, *buf_ce_dlpackbuffer;
static zend_object_handlers arraybuffer_handlers, foreign_handlers;

static zend_object *buffer_create(zend_class_entry *ce, zend_object_handlers *h)
{
	buf_buffer *b = zend_object_alloc(sizeof(buf_buffer), ce);

	zend_object_std_init(&b->std, ce);
	b->std.handlers = h;
	b->host = true;
	b->dev_type = kDLCPU;
	return &b->std;
}

static zend_object *arraybuffer_create(zend_class_entry *ce) { return buffer_create(ce, &arraybuffer_handlers); }
static zend_object *foreign_create(zend_class_entry *ce) { return buffer_create(ce, &foreign_handlers); }

static void buffer_free(zend_object *o)
{
	buf_buffer *b = buf_buffer_from(o);

	if (b->ready_dtor) {
		b->ready_dtor(b->ready_ctx);
	}
	if (b->deleter) {
		b->deleter(b->deleter_ctx);
	}
	if (b->owner) {
		OBJ_RELEASE(b->owner);
	}
	if (b->device) {
		OBJ_RELEASE(b->device);
	}
	if (b->alloc) {
		efree(b->alloc);
	}
	zend_object_std_dtor(o);
}

static zend_function *foreign_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "DLPackBuffer cannot be instantiated; use BufferView::fromDLPack()");
	return NULL;
}

/* A buffer over memory that is not ours. The deleter (if any) runs exactly once, when this object is freed. */
zend_object *buf_foreign_buffer_new(uint8_t *base, size_t len, bool writable, zend_object *device,
	void (*deleter)(void *), void *ctx, zend_object *owner)
{
	zend_object *o;
	buf_buffer *b;
	int32_t type, id;
	bool host;

	if (!buf_device_query(device, &type, &id, &host)) {
		return NULL;
	}
	o = foreign_create(buf_ce_dlpackbuffer);
	b = buf_buffer_from(o);
	b->base = base;
	b->len = len;
	b->writable = writable;
	b->deleter = deleter;
	b->deleter_ctx = ctx;
	b->dev_type = type;
	b->dev_id = id;
	b->host = host;
	b->slow = !host;
	if (device) {
		GC_ADDREF(device);
		b->device = device;
	}
	if (owner) {
		GC_ADDREF(owner);
		b->owner = owner;
	}
	return o;
}

/* Runs the readiness hook if the buffer is pending. The flag is cleared while the hook runs, so the hook
 * may use views of the buffer; it is set again if the hook fails. */
zend_result buf_buffer_make_ready(buf_buffer *b)
{
	zend_result r;

	if (EXPECTED(!b->pending)) {
		return SUCCESS;
	}
	b->pending = false;
	GC_ADDREF(&b->std);
	r = b->ready(&b->std, b->ready_ctx);
	if (r == SUCCESS && EG(exception)) {
		r = FAILURE;
	}
	if (r == FAILURE) {
		b->pending = true;
		if (!EG(exception)) {
			zend_throw_error(NULL, "The buffer is not ready");
		}
	}
	OBJ_RELEASE(&b->std);
	return r;
}

#define BUF buf_buffer_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(Buffers_ArrayBuffer, __construct)
{
	zend_long len;
	buf_buffer *b = BUF;

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
	b->alloc = ecalloc(1, (size_t)len + BUF_ALIGN);
	b->base = (uint8_t *)(((uintptr_t)b->alloc + BUF_ALIGN - 1) & ~(uintptr_t)(BUF_ALIGN - 1));
	b->len = (size_t)len;
	b->writable = true;
}

PHP_METHOD(Buffers_ArrayBuffer, fromString)
{
	zend_string *s;
	buf_buffer *b;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(s)
	ZEND_PARSE_PARAMETERS_END();

	object_init_ex(return_value, buf_ce_arraybuffer);
	b = buf_buffer_from(Z_OBJ_P(return_value));
	b->alloc = ecalloc(1, ZSTR_LEN(s) + BUF_ALIGN);
	b->base = (uint8_t *)(((uintptr_t)b->alloc + BUF_ALIGN - 1) & ~(uintptr_t)(BUF_ALIGN - 1));
	b->len = ZSTR_LEN(s);
	b->writable = true;
	memcpy(b->base, ZSTR_VAL(s), ZSTR_LEN(s));
}

PHP_METHOD(Buffers_ArrayBuffer, toString)
{
	buf_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_NONE();
	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) {
		RETURN_THROWS();
	}
	RETURN_STRINGL((const char *)b->base, b->len);
}

#define COMMON(cls) \
	PHP_METHOD(cls, byteLength) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG((zend_long)BUF->len); } \
	PHP_METHOD(cls, isWritable) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(BUF->writable); } \
	PHP_METHOD(cls, device) { \
		buf_buffer *b = BUF; \
		ZEND_PARSE_PARAMETERS_NONE(); \
		RETURN_OBJ_COPY(b->device ? b->device : buffers_cpu_device()); } \
	PHP_METHOD(cls, view) { buf_buffer_view_impl(Z_OBJ_P(ZEND_THIS), INTERNAL_FUNCTION_PARAM_PASSTHRU); }
COMMON(Buffers_ArrayBuffer)
COMMON(Buffers_DLPackBuffer)

static zend_class_entry *register_buffer(const char *name, const zend_function_entry *methods,
	zend_object *(*create)(zend_class_entry *), zend_object_handlers *h)
{
	zend_class_entry ce, *c;

	INIT_CLASS_ENTRY_EX(ce, name, strlen(name), methods);
	c = zend_register_internal_class_ex(&ce, NULL);
	c->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	c->create_object = create;
	zend_class_implements(c, 1, buf_ce_buffer_if);
	memcpy(h, &std_object_handlers, sizeof(zend_object_handlers));
	h->offset = XtOffsetOf(buf_buffer, std);
	h->free_obj = buffer_free;
	h->clone_obj = NULL;
	h->compare = buf_compare_identity;
	return c;
}

void buf_buffer_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Buffers\\BufferInterface", class_Buffers_BufferInterface_methods);
	buf_ce_buffer_if = zend_register_internal_interface(&ce);
	buf_ce_arraybuffer = register_buffer("Buffers\\ArrayBuffer", class_Buffers_ArrayBuffer_methods, arraybuffer_create, &arraybuffer_handlers);
	buf_ce_dlpackbuffer = register_buffer("Buffers\\DLPackBuffer", class_Buffers_DLPackBuffer_methods, foreign_create, &foreign_handlers);
	foreign_handlers.get_constructor = foreign_no_constructor;
}
