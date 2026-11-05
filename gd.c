/* Tensor::fromGdImage() and Tensor::toGdImage(), built when gd.h was found (see config.m4).
 *
 * ext/gd is only needed at run time and is not linked: the function that turns a GdImage object into a gdImagePtr is looked up in
 * the loaded gd module, and a GdImage is made by calling imagecreatetruecolor(), so nothing here depends on gd's private structs
 * except the public gdImage fields of gd.h. Those must match the gd.h that ext/gd was built with, which is checked on every call:
 * the GD_MAJOR_VERSION, GD_MINOR_VERSION and GD_RELEASE_VERSION constants that ext/gd registers are the version of the header it
 * was compiled against (the same value as ours when the headers match), which is what decides the struct layout. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "zend_constants.h"
#include "ext/standard/info.h"
#include <dlfcn.h>
#include <gd.h>

typedef gdImagePtr (*gd_ptr_fn)(zval *);

/* true if ext/gd is loaded and built against the same gd.h version as this extension; false after throwing */
static bool gd_check(void)
{
	zval *v[3];
	static const char *const names[3] = { "GD_MAJOR_VERSION", "GD_MINOR_VERSION", "GD_RELEASE_VERSION" };
	static const long ours[3] = { GD_MAJOR_VERSION, GD_MINOR_VERSION, GD_RELEASE_VERSION };

	if (!zend_hash_str_exists(&module_registry, "gd", sizeof("gd") - 1)) {
		zend_throw_error(NULL, "ext/gd is not loaded");
		return false;
	}
	for (int i = 0; i < 3; i++) {
		v[i] = zend_get_constant_str(names[i], strlen(names[i]));
		if (!v[i] || Z_TYPE_P(v[i]) != IS_LONG) {
			zend_throw_error(NULL, "Cannot determine the version of GD that ext/gd was built with (%s is not defined)", names[i]);
			return false;
		}
	}
	if (Z_LVAL_P(v[0]) != ours[0] || Z_LVAL_P(v[1]) != ours[1] || Z_LVAL_P(v[2]) != ours[2]) {
		zend_throw_error(NULL, "ext/buffers was built against GD %ld.%ld.%ld but ext/gd reports GD %ld.%ld.%ld; rebuild ext/buffers with the matching gd.h",
			ours[0], ours[1], ours[2], (long)Z_LVAL_P(v[0]), (long)Z_LVAL_P(v[1]), (long)Z_LVAL_P(v[2]));
		return false;
	}
	return true;
}

/* the gdImagePtr of a GdImage object, or NULL after throwing */
static gdImagePtr gd_image_of(zval *obj)
{
	zend_module_entry *m = zend_hash_str_find_ptr(&module_registry, "gd", sizeof("gd") - 1);
	gd_ptr_fn fn;
	gdImagePtr im;

	if (!m || !(fn = (gd_ptr_fn)dlsym(m->handle ? m->handle : RTLD_DEFAULT, "php_gd_libgdimageptr_from_zval_p"))) {
		zend_throw_error(NULL, "ext/gd is not loaded");
		return NULL;
	}
	im = fn(obj);
	if (!im) {
		zend_throw_error(NULL, "The GdImage has no image data");
	}
	return im;
}

/* gd alpha a (0 opaque .. 127 transparent) -> 255 .. 0: (127 - a) * 255 / 127 rounded to nearest, which is 2 * a' + (a' >> 6) for a' = 127 - a.
 * Plain integer arithmetic, so that the compilers can vectorize the pixel loops. */
static zend_always_inline uint32_t gd_alpha_to_8(uint32_t a)
{
	uint32_t r = 127 - (a & 0x7f);
	return 2 * r + (r >> 6);
}

/* 255 opaque .. 0 transparent -> gd 127 .. 0: 127 - round(A * 127 / 255); x / 255 is (x + (x >> 8) + 1) >> 8 for x < 65535 (both checked for all A) */
static zend_always_inline uint32_t gd_alpha_from_8(uint32_t a)
{
	uint32_t x = 127 * a + 127;
	return 127 - ((x + (x >> 8) + 1) >> 8);
}

/* the RGBA bytes of a pixel as one value: R | G << 8 | B << 16 | A << 24 (the bytes of memory in that order on a little-endian host) */
static zend_always_inline uint32_t gd_to_rgba(uint32_t px)
{
	return ((px >> 16) & 0xff) | (px & 0xff00) | ((px & 0xff) << 16) | (gd_alpha_to_8(px >> 24) << 24);
}

/* the contiguous rows, one 32-bit store per pixel; O3 so that GCC vectorizes them at the default -O2 as well */
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC push_options
# pragma GCC optimize("O3")
#endif
static void rows_gd_to_rgba(uint32_t *restrict d, const uint32_t *restrict s, int w)
{
	for (int x = 0; x < w; x++) d[x] = gd_to_rgba(s[x]);
}

static void rows_rgba_to_gd(uint32_t *restrict d, const uint32_t *restrict s, int64_t w)
{
	for (int64_t x = 0; x < w; x++) {
		uint32_t p = s[x];
		d[x] = (gd_alpha_from_8(p >> 24) << 24) | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
	}
}
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC pop_options
#endif

PHP_METHOD(Buffers_Tensor, fromGdImage)
{
	zval *obj;
	zend_object *into = NULL, *buf = NULL, *view = NULL;
	gdImagePtr im;
	buf_view *v;
	int64_t sb0, sb1, sb2;
	uint32_t pal[gdMaxColors];
	bool fast, tc;
	int w, h;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_ZVAL(obj)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(into, buf_ce_tensor)
	ZEND_PARSE_PARAMETERS_END();
	if (Z_TYPE_P(obj) != IS_OBJECT || !zend_string_equals_literal(Z_OBJCE_P(obj)->name, "GdImage")) {
		zend_argument_type_error(1, "must be of type GdImage, %s given", zend_zval_type_name(obj));
		RETURN_THROWS();
	}
	if (!gd_check() || !(im = gd_image_of(obj))) RETURN_THROWS();

	if (into) {
		v = buf_view_from(into);
		if (v->dt != BUF_T_UINT8 || v->ndim != 3 || v->shape[0] != im->sy || v->shape[1] != im->sx || v->shape[2] != 4) {
			char got[96];
			size_t n = 0;

			got[n++] = '[';
			for (int i = 0; i < v->ndim && n < sizeof got - 24; i++) {
				n += snprintf(got + n, sizeof got - n, i ? ", %" PRId64 : "%" PRId64, v->shape[i]);
			}
			got[n++] = ']';
			got[n] = 0;
			zend_argument_value_error(2, "must be a UInt8 tensor of shape [%d, %d, 4], got %s with shape %s",
				im->sy, im->sx, buf_dtypes[v->dt].name, got);
			RETURN_THROWS();
		}
		if (!buf_view_check_access(v)) RETURN_THROWS();
		if (!v->writable) {
			zend_throw_error(NULL, "Cannot write to a read-only Tensor");
			RETURN_THROWS();
		}
	} else {
		buf = buf_buffer_new_host_uninit((uint64_t)im->sy * (uint64_t)im->sx * 4);   /* every byte is written below */
		if (!buf) RETURN_THROWS();
		view = buf_view_new(buf, BUF_T_UINT8, 3, (int64_t[]){im->sy, im->sx, 4}, (int64_t[]){(int64_t)im->sx * 4, 4, 1},
			buf_buffer_from(buf)->base, true, false);
		v = buf_view_from(view);
	}
	sb0 = v->sbytes[0]; sb1 = v->sbytes[1]; sb2 = v->sbytes[2];
#ifdef WORDS_BIGENDIAN
	fast = false;
#else
	fast = sb1 == 4 && sb2 == 1 && !(((uintptr_t)v->data | (uintptr_t)sb0) & 3);   /* one aligned 32-bit store per pixel */
#endif

	w = im->sx;
	h = im->sy;
	tc = im->trueColor;
	if (!tc) {
		for (int i = 0; i < gdMaxColors; i++) {
			pal[i] = (uint32_t)im->red[i] | (uint32_t)im->green[i] << 8 | (uint32_t)im->blue[i] << 16 | gd_alpha_to_8(im->alpha[i]) << 24;
		}
	}
	for (int y = 0; y < h; y++) {
		uint8_t *row = v->data + y * sb0;

		if (fast) {
			uint32_t *restrict d = (uint32_t *)row;

			if (tc) {
				const uint32_t *restrict s = (const uint32_t *)im->tpixels[y];
				rows_gd_to_rgba(d, s, w);
			} else {
				const unsigned char *restrict s = im->pixels[y];
				for (int x = 0; x < w; x++) d[x] = pal[s[x]];
			}
		} else {
			for (int x = 0; x < w; x++) {
				uint32_t c = tc ? gd_to_rgba((uint32_t)im->tpixels[y][x]) : pal[im->pixels[y][x]];
				uint8_t *p = row + x * sb1;

				p[0] = (uint8_t)c;
				p[sb2] = (uint8_t)(c >> 8);
				p[2 * sb2] = (uint8_t)(c >> 16);
				p[3 * sb2] = (uint8_t)(c >> 24);
			}
		}
	}
	if (into) {
		RETURN_OBJ_COPY(into);
	}
	OBJ_RELEASE(buf);
	RETURN_OBJ(view);
}

PHP_METHOD(Buffers_Tensor, toGdImage)
{
	buf_view *v = buf_view_from(Z_OBJ_P(ZEND_THIS));
	zend_function *create;
	zval params[2], img;
	gdImagePtr im;
	int64_t h, w, sb0, sb1, sb2;
	int channels;

	ZEND_PARSE_PARAMETERS_NONE();
	if (v->dt != BUF_T_UINT8) {
		zend_throw_error(NULL, "Tensor::toGdImage() needs a UInt8 tensor, this one is %s", buf_dtypes[v->dt].name);
		RETURN_THROWS();
	}
	channels = v->ndim == 2 ? 1 : v->ndim == 3 ? (int)v->shape[2] : 0;
	if (!(v->ndim == 2 || (v->ndim == 3 && (channels == 3 || channels == 4)))) {
		zend_throw_error(NULL, "Tensor::toGdImage() needs the shape [height, width], [height, width, 3] or [height, width, 4]");
		RETURN_THROWS();
	}
	h = v->shape[0];
	w = v->shape[1];
	if (h < 1 || w < 1 || h > INT_MAX || w > INT_MAX) {
		zend_throw_error(NULL, "Tensor::toGdImage(): the height and width must be between 1 and %d", INT_MAX);
		RETURN_THROWS();
	}
	if (!gd_check() || !buf_view_check_access(v)) RETURN_THROWS();
	sb0 = v->sbytes[0];
	sb1 = v->sbytes[1];
	sb2 = channels > 1 ? v->sbytes[2] : 0;

	create = zend_hash_str_find_ptr(CG(function_table), "imagecreatetruecolor", sizeof("imagecreatetruecolor") - 1);
	if (!create) {
		zend_throw_error(NULL, "ext/gd is not loaded");
		RETURN_THROWS();
	}
	ZVAL_LONG(&params[0], w);
	ZVAL_LONG(&params[1], h);
	zend_call_known_function(create, NULL, NULL, &img, 2, params, NULL);
	if (EG(exception)) RETURN_THROWS();
	if (Z_TYPE(img) != IS_OBJECT) {
		zend_throw_error(NULL, "Tensor::toGdImage(): could not create a %" PRId64 "x%" PRId64 " GD image", w, h);
		RETURN_THROWS();
	}
	if (!(im = gd_image_of(&img))) {
		zval_ptr_dtor(&img);
		RETURN_THROWS();
	}

	{
#ifdef WORDS_BIGENDIAN
		bool fast = false;
#else
		bool fast = channels == 4 && sb1 == 4 && sb2 == 1 && !(((uintptr_t)v->data | (uintptr_t)sb0) & 3);
#endif
		for (int64_t y = 0; y < h; y++) {
			const uint8_t *row = v->data + y * sb0;
			uint32_t *dst = (uint32_t *)im->tpixels[y];

			if (fast) {
				rows_rgba_to_gd(dst, (const uint32_t *)row, w);
			} else if (channels == 1) {
				for (int64_t x = 0; x < w; x++) {
					uint32_t g = row[x * sb1];
					dst[x] = (g << 16) | (g << 8) | g;
				}
			} else if (channels == 3) {
				for (int64_t x = 0; x < w; x++) {
					const uint8_t *p = row + x * sb1;
					dst[x] = ((uint32_t)p[0] << 16) | ((uint32_t)p[sb2] << 8) | p[2 * sb2];
				}
			} else {
				for (int64_t x = 0; x < w; x++) {
					const uint8_t *p = row + x * sb1;
					dst[x] = (gd_alpha_from_8(p[3 * sb2]) << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[sb2] << 8) | p[2 * sb2];
				}
			}
		}
	}
	if (channels == 4) {
		im->saveAlphaFlag = 1;
	}
	RETURN_COPY_VALUE(&img);
}
