/* Tensor::fromGdImage() and Tensor::toGdImage(), built when gd.h was found (see config.m4).
 *
 * ext/gd is only needed at run time and is not linked: the function that turns a GdImage object into a gdImagePtr is looked up in
 * the loaded gd module, and a GdImage is made by calling imagecreatetruecolor(), so nothing here depends on gd's private structs
 * except the public gdImage fields of gd.h, which must match the libgd that ext/gd runs with. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "ext/standard/info.h"
#include <dlfcn.h>
#include <gd.h>

typedef gdImagePtr (*gd_ptr_fn)(zval *);

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

PHP_METHOD(Buffers_Tensor, fromGdImage)
{
	zval *obj;
	gdImagePtr im;
	zend_object *buf, *view;
	int64_t shape[3];
	uint32_t pal[gdMaxColors];
	uint8_t *dst;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(obj)
	ZEND_PARSE_PARAMETERS_END();
	if (Z_TYPE_P(obj) != IS_OBJECT || !zend_string_equals_literal(Z_OBJCE_P(obj)->name, "GdImage")) {
		zend_argument_type_error(1, "must be of type GdImage, %s given", zend_zval_type_name(obj));
		RETURN_THROWS();
	}
	if (!(im = gd_image_of(obj))) RETURN_THROWS();

	shape[0] = gdImageSY(im);
	shape[1] = gdImageSX(im);
	shape[2] = 4;
	buf = buf_buffer_new_host((uint64_t)shape[0] * (uint64_t)shape[1] * 4);
	if (!buf) RETURN_THROWS();
	dst = buf_buffer_from(buf)->base;

	if (im->trueColor) {
		for (int y = 0; y < im->sy; y++) {
			const int *src = im->tpixels[y];

			for (int x = 0; x < im->sx; x++, dst += 4) {
				int px = src[x];
				/* gd alpha 0 (opaque) .. 127 (transparent) -> 255 .. 0, rounded to nearest */
				dst[0] = (uint8_t)gdTrueColorGetRed(px);
				dst[1] = (uint8_t)gdTrueColorGetGreen(px);
				dst[2] = (uint8_t)gdTrueColorGetBlue(px);
				dst[3] = (uint8_t)(((127 - gdTrueColorGetAlpha(px)) * 255 + 63) / 127);
			}
		}
	} else {
		for (int i = 0; i < gdMaxColors; i++) {
			pal[i] = (uint32_t)im->red[i] | (uint32_t)im->green[i] << 8 | (uint32_t)im->blue[i] << 16
				| (uint32_t)(((127 - (im->alpha[i] & 0x7f)) * 255 + 63) / 127) << 24;
		}
		for (int y = 0; y < im->sy; y++) {
			const unsigned char *src = im->pixels[y];

			for (int x = 0; x < im->sx; x++, dst += 4) {
				uint32_t c = pal[src[x]];

				dst[0] = (uint8_t)c;
				dst[1] = (uint8_t)(c >> 8);
				dst[2] = (uint8_t)(c >> 16);
				dst[3] = (uint8_t)(c >> 24);
			}
		}
	}
	view = buf_view_new(buf, BUF_T_UINT8, 3, shape, (int64_t[]){shape[1] * 4, 4, 1}, buf_buffer_from(buf)->base, true, false);
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
	if (!buf_view_check_access(v)) RETURN_THROWS();
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

	for (int64_t y = 0; y < h; y++) {
		const uint8_t *row = v->data + y * sb0;
		int *dst = im->tpixels[y];

		if (channels == 1) {
			for (int64_t x = 0; x < w; x++) {
				int g = row[x * sb1];
				dst[x] = (g << 16) | (g << 8) | g;
			}
		} else if (channels == 3) {
			for (int64_t x = 0; x < w; x++) {
				const uint8_t *p = row + x * sb1;
				dst[x] = (p[0] << 16) | (p[sb2] << 8) | p[2 * sb2];
			}
		} else {
			for (int64_t x = 0; x < w; x++) {
				const uint8_t *p = row + x * sb1;
				int a = 127 - (p[3 * sb2] * 254 + 255) / 510;   /* 255 opaque .. 0 transparent -> gd 0 .. 127, rounded to nearest */
				dst[x] = (a << 24) | (p[0] << 16) | (p[sb2] << 8) | p[2 * sb2];
			}
		}
	}
	if (channels == 4) {
		im->saveAlphaFlag = 1;
	}
	RETURN_COPY_VALUE(&img);
}
