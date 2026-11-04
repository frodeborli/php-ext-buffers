/* Buffer: bytes on some device. See php_buffers.h for what a buffer can own. */
#ifndef _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 64   /* files larger than 2 GB on 32-bit systems */
#endif
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "ext/standard/php_filestat.h"
#include "main/fopen_wrappers.h"
#include "main/php_streams.h"
#include "buffers_arginfo.h"

#ifdef _WIN32
# include <windows.h>
#else
# include <fcntl.h>
# include <unistd.h>
# include <sys/mman.h>
# include <sys/stat.h>
#endif

zend_class_entry *buf_ce_buffer;
static zend_object_handlers buffer_handlers;
static uint8_t empty_byte; /* base of a zero-length file buffer */

static zend_object *buffer_create(zend_class_entry *ce)
{
	buf_buffer *b = zend_object_alloc(sizeof(buf_buffer), ce);

	zend_object_std_init(&b->std, ce);
	b->std.handlers = &buffer_handlers;
	b->host = true;
	b->dev_type = kDLCPU;
	b->file = -1;
	return &b->std;
}

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
	if (b->map_base) {
#ifdef _WIN32
		UnmapViewOfFile(b->map_base);
#else
		munmap(b->map_base, b->map_len);
#endif
	}
	if (b->file != -1) {
#ifdef _WIN32
		CloseHandle((HANDLE)b->file);
#else
		close((int)b->file);
#endif
	}
	zend_object_std_dtor(o);
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
	o = buffer_create(buf_ce_buffer);
	b = buf_buffer_from(o);
	b->inited = true;
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

/* is the buffer on this device? (the same object, or the same DLPack device type and id) */
bool buf_buffer_on_device(buf_buffer *b, zend_object *device)
{
	int32_t type, id;
	bool host;
	zend_object *own = b->device ? b->device : buffers_cpu_device();

	if (own == device) return true;
	if (!buf_device_query(device, &type, &id, &host)) {
		zend_clear_exception();
		return false;
	}
	return type > 0 && type == b->dev_type && id == b->dev_id;
}

/* reads n bytes at `offset` of the buffer from the file of an unmapped file buffer */
zend_result buf_buffer_read_file(buf_buffer *b, uint64_t offset, void *dst, size_t n)
{
	uint64_t at = b->file_off + offset;
	uint8_t *p = dst;

	while (n) {
#ifdef _WIN32
		OVERLAPPED ov = {0};
		DWORD got = 0, want = n > 0x40000000 ? 0x40000000 : (DWORD)n;

		ov.Offset = (DWORD)(at & 0xffffffffu);
		ov.OffsetHigh = (DWORD)(at >> 32);
		if (!ReadFile((HANDLE)b->file, p, want, &got, &ov) || got == 0) {
			zend_throw_error(NULL, "Cannot read the file (error %lu)", (unsigned long)GetLastError());
			return FAILURE;
		}
#else
		ssize_t got = pread((int)b->file, p, n, (off_t)at);

		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) {
			zend_throw_error(NULL, "Cannot read the file: %s", got < 0 ? strerror(errno) : "it was truncated");
			return FAILURE;
		}
#endif
		p += got;
		at += got;
		n -= (size_t)got;
	}
	return SUCCESS;
}

/* a zero-filled host Buffer of `bytes` (>= 0), or NULL after throwing */
zend_object *buf_buffer_new_host(uint64_t bytes)
{
	zend_object *o;
	buf_buffer *b;

	if (bytes > (uint64_t)ZEND_LONG_MAX / 2 || bytes > (uint64_t)SIZE_MAX / 2) {
		zend_throw_error(NULL, "The result is too large to allocate");
		return NULL;
	}
	o = buffer_create(buf_ce_buffer);
	b = buf_buffer_from(o);
	b->alloc = ecalloc(1, (size_t)bytes + BUF_ALIGN);
	b->base = (uint8_t *)(((uintptr_t)b->alloc + BUF_ALIGN - 1) & ~(uintptr_t)(BUF_ALIGN - 1));
	b->len = bytes;
	b->writable = true;
	b->inited = true;
	return o;
}

#define BUF buf_buffer_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(Buffers_Buffer, __construct)
{
	zend_long len;
	buf_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(len)
	ZEND_PARSE_PARAMETERS_END();

	if (b->inited) {
		zend_throw_error(NULL, "Buffer is already constructed");
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
	b->inited = true;
}

PHP_METHOD(Buffers_Buffer, fromString)
{
	zend_string *s;
	buf_buffer *b;
	bool writable = true;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_STR(s)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(writable)
	ZEND_PARSE_PARAMETERS_END();

	object_init_ex(return_value, buf_ce_buffer);
	b = buf_buffer_from(Z_OBJ_P(return_value));
	b->alloc = ecalloc(1, ZSTR_LEN(s) + BUF_ALIGN);
	b->base = (uint8_t *)(((uintptr_t)b->alloc + BUF_ALIGN - 1) & ~(uintptr_t)(BUF_ALIGN - 1));
	b->len = ZSTR_LEN(s);
	b->writable = writable;
	b->inited = true;
	memcpy(b->base, ZSTR_VAL(s), ZSTR_LEN(s));
}

/* ---- files ------------------------------------------------------------------------------------ */

enum { FILE_READ, FILE_WRITE, FILE_CREATE };

/* Opens `path` for reading, reading and writing, or creating/truncating (then sized to `create_len`); returns the handle
 * (or -1 after throwing) and the file size */
static intptr_t file_open(const char *path, int mode, uint64_t create_len, uint64_t *size)
{
	if (php_check_open_basedir_ex(path, 0)) {
		zend_throw_error(NULL, "Cannot open '%s': open_basedir restriction in effect", path);
		return -1;
	}
#ifdef _WIN32
	{
		LARGE_INTEGER sz;
		HANDLE h = CreateFileA(path, mode == FILE_READ ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			mode == FILE_CREATE ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (h == INVALID_HANDLE_VALUE) {
			zend_throw_error(NULL, "Cannot open '%s' (error %lu)", path, (unsigned long)GetLastError());
			return -1;
		}
		if (mode == FILE_CREATE) {
			sz.QuadPart = (LONGLONG)create_len;
			if (!SetFilePointerEx(h, sz, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
				CloseHandle(h);
				zend_throw_error(NULL, "Cannot size '%s' to " ZEND_LONG_FMT " bytes", path, (zend_long)create_len);
				return -1;
			}
		}
		if (!GetFileSizeEx(h, &sz)) {
			CloseHandle(h);
			zend_throw_error(NULL, "Cannot get the size of '%s'", path);
			return -1;
		}
		*size = (uint64_t)sz.QuadPart;
		return (intptr_t)h;
	}
#else
	{
		struct stat st;
		int flags = (mode == FILE_READ ? O_RDONLY : O_RDWR) | (mode == FILE_CREATE ? O_CREAT | O_TRUNC : 0)
# ifdef O_CLOEXEC
			| O_CLOEXEC
# endif
# ifdef O_LARGEFILE
			| O_LARGEFILE
# endif
		;
		int fd = VCWD_OPEN_MODE(path, flags, 0666);

		if (fd < 0) {
			zend_throw_error(NULL, "Cannot open '%s': %s", path, strerror(errno));
			return -1;
		}
		if (mode == FILE_CREATE && ftruncate(fd, (off_t)create_len) != 0) {
			close(fd);
			zend_throw_error(NULL, "Cannot size '%s' to " ZEND_LONG_FMT " bytes: %s", path, (zend_long)create_len, strerror(errno));
			return -1;
		}
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
			close(fd);
			zend_throw_error(NULL, "'%s' is not a regular file", path);
			return -1;
		}
		*size = (uint64_t)st.st_size;
		return fd;
	}
#endif
}

static void buf_file_close(intptr_t file)
{
#ifdef _WIN32
	CloseHandle((HANDLE)file);
#else
	close((int)file);
#endif
}

/* Maps [offset, offset + len) (shared: other processes see writes at once, and a writable mapping writes the file);
 * false if that is impossible (a read-only caller then falls back to an unmapped buffer) */
static bool file_map(buf_buffer *b, intptr_t file, uint64_t offset, uint64_t len, bool writable)
{
	uint64_t gran, aligned, delta;
	void *p;

#ifdef _WIN32
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	gran = si.dwAllocationGranularity;
#else
	gran = (uint64_t)sysconf(_SC_PAGESIZE);
#endif
	aligned = offset - offset % gran;
	delta = offset - aligned;
	if (len + delta > (uint64_t)SIZE_MAX || len + delta < len) {
		return false;
	}
#ifdef _WIN32
	{
		HANDLE m = CreateFileMappingA((HANDLE)file, NULL, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, NULL);

		if (!m) return false;
		p = MapViewOfFile(m, writable ? FILE_MAP_WRITE : FILE_MAP_READ, (DWORD)(aligned >> 32), (DWORD)(aligned & 0xffffffffu), (size_t)(len + delta));
		CloseHandle(m);
		if (!p) return false;
	}
#else
	p = mmap(NULL, (size_t)(len + delta), writable ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, (int)file, (off_t)aligned);
	if (p == MAP_FAILED) return false;
#endif
	b->map_base = p;
	b->map_len = (size_t)(len + delta);
	b->base = (uint8_t *)p + delta;
	return true;
}

/* a buffer on FileDevice over [offset, offset + len) of `file`, which this takes over (closed unless the buffer keeps it).
 * Returns false after throwing. */
static bool file_buffer(zval *rv, intptr_t file, uint64_t offset, uint64_t len, bool map, bool writable)
{
	buf_buffer *b;

	object_init_ex(rv, buf_ce_buffer);
	b = buf_buffer_from(Z_OBJ_P(rv));
	b->inited = true;
	b->writable = writable;
	b->len = len;
	b->dev_type = BUF_DEV_FILE;
	b->device = buf_file_device();
	GC_ADDREF(b->device);
	if (len == 0) {
		b->base = &empty_byte;
		b->host = map;
		b->slow = !map;
		buf_file_close(file);
	} else if (map && file_map(b, file, offset, len, writable)) {
		b->host = true;
		b->slow = false;
		buf_file_close(file);   /* the mapping keeps the file */
	} else if (writable) {
		buf_file_close(file);
		zval_ptr_dtor(rv);
		ZVAL_UNDEF(rv);
		zend_throw_error(NULL, "Cannot map the file for writing");
		return false;
	} else {
		b->host = false;        /* storage only: Tensor::to() reads it */
		b->slow = true;
		b->file = file;
		b->file_off = offset;
	}
	return true;
}

PHP_METHOD(Buffers_Buffer, fromFile)
{
	zend_string *path;
	zend_long offset = 0, length = 0;
	bool length_null = true, map = true, writable = false;
	uint64_t size, len;
	intptr_t file;

	ZEND_PARSE_PARAMETERS_START(1, 5)
		Z_PARAM_PATH_STR(path)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(offset)
		Z_PARAM_LONG_OR_NULL(length, length_null)
		Z_PARAM_BOOL(map)
		Z_PARAM_BOOL(writable)
	ZEND_PARSE_PARAMETERS_END();

	if (offset < 0) {
		zend_argument_value_error(2, "must be greater than or equal to 0");
		RETURN_THROWS();
	}
	if (!length_null && length < 0) {
		zend_argument_value_error(3, "must be greater than or equal to 0");
		RETURN_THROWS();
	}
	if (writable && !map) {
		zend_argument_value_error(5, "cannot be true with $map false: writable file buffers must be mapped");
		RETURN_THROWS();
	}
	file = file_open(ZSTR_VAL(path), writable ? FILE_WRITE : FILE_READ, 0, &size);
	if (file == -1) {
		RETURN_THROWS();
	}
	if ((uint64_t)offset > size) {
		buf_file_close(file);
		zend_argument_value_error(2, "is beyond the end of the file (%" PRIu64 " bytes)", size);
		RETURN_THROWS();
	}
	len = length_null ? size - (uint64_t)offset : (uint64_t)length;
	if (len > size - (uint64_t)offset) {
		buf_file_close(file);
		zend_argument_value_error(3, "extends beyond the end of the file (%" PRIu64 " bytes, from offset " ZEND_LONG_FMT ")", size, offset);
		RETURN_THROWS();
	}
	if (!file_buffer(return_value, file, (uint64_t)offset, len, map, writable)) {
		RETURN_THROWS();
	}
}

PHP_METHOD(Buffers_Buffer, createFile)
{
	zend_string *path;
	zend_long length;
	uint64_t size;
	intptr_t file;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_PATH_STR(path)
		Z_PARAM_LONG(length)
	ZEND_PARSE_PARAMETERS_END();

	if (length < 0) {
		zend_argument_value_error(2, "must be greater than or equal to 0");
		RETURN_THROWS();
	}
	if ((uint64_t)length > (uint64_t)SIZE_MAX / 2) {
		zend_argument_value_error(2, "is too large to map on this platform");
		RETURN_THROWS();
	}
	file = file_open(ZSTR_VAL(path), FILE_CREATE, (uint64_t)length, &size);
	if (file == -1) {
		RETURN_THROWS();
	}
	if (!file_buffer(return_value, file, 0, (uint64_t)length, true, true)) {
		RETURN_THROWS();
	}
}

PHP_METHOD(Buffers_Buffer, flush)
{
	buf_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_NONE();
	if (!b->map_base || !b->writable) {
		return;
	}
#ifdef _WIN32
	if (!FlushViewOfFile(b->map_base, b->map_len)) {
		zend_throw_error(NULL, "Cannot flush the mapping (error %lu)", (unsigned long)GetLastError());
	}
#else
	if (msync(b->map_base, b->map_len, MS_SYNC) != 0) {
		zend_throw_error(NULL, "Cannot flush the mapping: %s", strerror(errno));
	}
#endif
}

/* ---- streams ---------------------------------------------------------------------------------- */

/* the memory of a range of a host-accessible buffer; false after throwing */
static bool stream_range(buf_buffer *b, zend_long offset, bool length_null, zend_long length, bool need_writable, uint8_t **p, size_t *n)
{
	uint64_t avail;

	if (!b->host) {
		zend_string *name = buf_device_name(b->dev_type, b->dev_id);

		zend_throw_error(NULL, "Cannot access the bytes of a buffer on device %s: its memory is not host accessible; transfer to the host first with Tensor::to()", ZSTR_VAL(name));
		zend_string_release(name);
		return false;
	}
	if (need_writable && !b->writable) {
		zend_throw_error(NULL, "Cannot write to a read-only Buffer");
		return false;
	}
	if (offset < 0 || (uint64_t)offset > b->len) {
		zend_argument_value_error(2, "must be between 0 and the byte length of the buffer");
		return false;
	}
	avail = b->len - (uint64_t)offset;
	if (!length_null && (length < 0 || (uint64_t)length > avail)) {
		zend_argument_value_error(3, "must be between 0 and the bytes left after the offset (%" PRIu64 ")", avail);
		return false;
	}
	if (buf_buffer_make_ready(b) == FAILURE) {
		return false;
	}
	*n = (size_t)(length_null ? avail : (uint64_t)length);
	*p = b->base + offset;
	return true;
}

PHP_METHOD(Buffers_Buffer, writeTo)
{
	zval *zstream;
	zend_long offset = 0, length = 0;
	bool length_null = true;
	php_stream *stream;
	uint8_t *p;
	size_t n, done = 0;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_RESOURCE(zstream)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(offset)
		Z_PARAM_LONG_OR_NULL(length, length_null)
	ZEND_PARSE_PARAMETERS_END();

	php_stream_from_zval(stream, zstream);
	if (!stream_range(BUF, offset, length_null, length, false, &p, &n)) {
		RETURN_THROWS();
	}
	while (done < n) {
		ssize_t r = php_stream_write(stream, (const char *)p + done, n - done);

		if (r <= 0) {
			if (!done) {
				zend_throw_error(NULL, "Cannot write to the stream");
				RETURN_THROWS();
			}
			break;   /* a short write that cannot continue: report what went out */
		}
		done += (size_t)r;
	}
	RETURN_LONG((zend_long)done);
}

PHP_METHOD(Buffers_Buffer, readFrom)
{
	zval *zstream;
	zend_long offset = 0, length = 0;
	bool length_null = true;
	php_stream *stream;
	uint8_t *p;
	size_t n, done = 0;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_RESOURCE(zstream)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(offset)
		Z_PARAM_LONG_OR_NULL(length, length_null)
	ZEND_PARSE_PARAMETERS_END();

	php_stream_from_zval(stream, zstream);
	if (!stream_range(BUF, offset, length_null, length, true, &p, &n)) {
		RETURN_THROWS();
	}
	while (done < n) {
		ssize_t r = php_stream_read(stream, (char *)p + done, n - done);

		if (r <= 0) {
			break;   /* end of the stream */
		}
		done += (size_t)r;
	}
	RETURN_LONG((zend_long)done);
}

PHP_METHOD(Buffers_Buffer, toString)
{
	buf_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_NONE();
	if (!b->host) {
		zend_string *n = buf_device_name(b->dev_type, b->dev_id);

		zend_throw_error(NULL, "Cannot read the bytes of a buffer on device %s: its memory is not host accessible; transfer to the host first with Tensor::to()", ZSTR_VAL(n));
		zend_string_release(n);
		RETURN_THROWS();
	}
	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) {
		RETURN_THROWS();
	}
	if (b->len > ZEND_LONG_MAX) {
		zend_throw_error(NULL, "The buffer is too large for a string");
		RETURN_THROWS();
	}
	RETURN_STRINGL((const char *)b->base, (size_t)b->len);
}

PHP_METHOD(Buffers_Buffer, isWritable) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(BUF->writable); }
PHP_METHOD(Buffers_Buffer, isHostAccessible) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(BUF->host); }

PHP_METHOD(Buffers_Buffer, view) { buf_buffer_view_impl(Z_OBJ_P(ZEND_THIS), INTERNAL_FUNCTION_PARAM_PASSTHRU); }

/* $byteLength and $device: read-only virtual properties (internal get hooks, see props.c) */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_buf_int, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_buf_device, 0, 0, Buffers\\Device, 0)
ZEND_END_ARG_INFO()

static ZEND_NAMED_FUNCTION(hook_byte_length)
{
	buf_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_NONE();
	if (b->len > (uint64_t)ZEND_LONG_MAX) {
		zend_throw_error(NULL, "The byte length does not fit a PHP int on this platform");
		RETURN_THROWS();
	}
	RETURN_LONG((zend_long)b->len);
}

static ZEND_NAMED_FUNCTION(hook_device)
{
	buf_buffer *b = BUF;

	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_OBJ_COPY(b->device ? b->device : buffers_cpu_device());
}

/* var_dump(): the size, the device by name, and the flags */
static HashTable *buffer_debug_info(zend_object *o, int *is_temp)
{
	buf_buffer *b = buf_buffer_from(o);
	HashTable *ht;
	zval z;

	ALLOC_HASHTABLE(ht);
	zend_hash_init(ht, 4, NULL, ZVAL_PTR_DTOR, 0);
	if (b->len > (uint64_t)ZEND_LONG_MAX) ZVAL_STR(&z, zend_strpprintf(0, "%" PRIu64, b->len)); else ZVAL_LONG(&z, (zend_long)b->len);
	zend_hash_str_add_new(ht, "byteLength", sizeof("byteLength") - 1, &z);
	ZVAL_STR(&z, buf_device_name(b->dev_type, b->dev_id));
	zend_hash_str_add_new(ht, "device", sizeof("device") - 1, &z);
	ZVAL_BOOL(&z, b->writable);
	zend_hash_str_add_new(ht, "writable", sizeof("writable") - 1, &z);
	ZVAL_BOOL(&z, b->host);
	zend_hash_str_add_new(ht, "hostAccessible", sizeof("hostAccessible") - 1, &z);
	*is_temp = 1;
	return ht;
}

void buf_buffer_minit(void)
{
	zend_class_entry ce;
	const buf_prop_def props[] = {
		{ "byteLength", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_byte_length, ai_buf_int, 0 },
		{ "device", (zend_type) ZEND_TYPE_INIT_CLASS(zend_string_init("Buffers\\Device", sizeof("Buffers\\Device") - 1, 1), 0, 0), hook_device, ai_buf_device, 0 },
	};

	INIT_CLASS_ENTRY(ce, "Buffers\\Buffer", class_Buffers_Buffer_methods);
	buf_ce_buffer = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_buffer->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_buffer->create_object = buffer_create;
	buf_declare_hooked_props(buf_ce_buffer, props, 2);
	memcpy(&buffer_handlers, &std_object_handlers, sizeof buffer_handlers);
	buffer_handlers.offset = XtOffsetOf(buf_buffer, std);
	buffer_handlers.free_obj = buffer_free;
	buffer_handlers.clone_obj = NULL;
	buffer_handlers.compare = buf_compare_identity;
	buffer_handlers.get_debug_info = buffer_debug_info;
}
