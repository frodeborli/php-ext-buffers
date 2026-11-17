/* PHP arrays read and written in place by compiled code: lists of numbers or bools (nested lists of equal lengths
 * give more axes), whose packed zvals the code reads directly. The front end and the binding of a call both use these. */
#ifndef BUF_CC_ZARR_H
#define BUF_CC_ZARR_H

#include "php.h"

/* what an array holds, as the code reads it */
enum { AK_EMPTY, AK_INT, AK_FLOAT, AK_NUM /* ints and floats: read as floats */, AK_BOOL };

/* what was seen in the leaves */
#define AS_INT 1u
#define AS_FLOAT 2u
#define AS_BOOL 4u
#define ZA_MAX_NDIM 16

enum { ZA_PEEK, ZA_READ, ZA_WRITE };

/* checks the array in *z and its nested arrays down to ndim axes, records the lengths in shape[] and what the leaves
 * hold in *seen. ZA_PEEK reads any list; ZA_READ wants packed lists without holes (1: one is not, the caller copies the
 * array and checks it again for writing); ZA_WRITE first separates every array that is shared (as PHP does before a
 * write) and packs a list that is not packed. 0: fine; -1: not a list of lists of ndim axes of equal lengths, or a leaf
 * that is not an int, a float or a bool. */
static inline int za_check(zval *z, int d, int ndim, int mode, int64_t *shape, unsigned *seen)
{
	HashTable *ht;
	uint32_t n;

	if (Z_TYPE_P(z) != IS_ARRAY) return -1;
	ht = Z_ARRVAL_P(z);
	n = zend_hash_num_elements(ht);
	/* (bit 8 + d of *seen: the length of axis d is known) */
	if (!(*seen & (1u << (8 + d)))) {
		shape[d] = n;
		*seen |= 1u << (8 + d);
	} else if (shape[d] != (int64_t)n) {
		return -1;
	}
	if (n == 0) {
		for (int e = d + 1; e < ndim; e++) {
			if (!(*seen & (1u << (8 + e)))) { shape[e] = 0; *seen |= 1u << (8 + e); }
			else if (shape[e] != 0) return -1;
		}
		return 0;
	}
	if (mode == ZA_WRITE) {
		if (!HT_IS_PACKED(ht) || !HT_IS_WITHOUT_HOLES(ht)) {
			zend_array *p;
			zval *v;

			if (!zend_array_is_list(ht)) return -1;
			p = zend_new_array(n);
			zend_hash_real_init_packed(p);
			ZEND_HASH_FILL_PACKED(p) {
				ZEND_HASH_FOREACH_VAL(ht, v) {
					Z_TRY_ADDREF_P(v);
					ZEND_HASH_FILL_ADD(v);
				} ZEND_HASH_FOREACH_END();
			} ZEND_HASH_FILL_END();
			zval_ptr_dtor(z);
			ZVAL_ARR(z, p);
		} else {
			SEPARATE_ARRAY(z);
		}
		ht = Z_ARRVAL_P(z);
	} else if (!HT_IS_PACKED(ht) || !HT_IS_WITHOUT_HOLES(ht)) {
		if (!zend_array_is_list(ht)) return -1;
		if (mode == ZA_READ) return 1;
		{
			zval *v;
			int r;

			ZEND_HASH_FOREACH_VAL(ht, v) {
				if (d < ndim - 1) {
					if ((r = za_check(v, d + 1, ndim, mode, shape, seen)) != 0) return r;
				} else {
					switch (Z_TYPE_P(v)) {
					case IS_LONG: *seen |= AS_INT; break;
					case IS_DOUBLE: *seen |= AS_FLOAT; break;
					case IS_TRUE: case IS_FALSE: *seen |= AS_BOOL; break;
					default: return -1;
					}
				}
			} ZEND_HASH_FOREACH_END();
			return 0;
		}
	}
	if (d < ndim - 1) {
		for (uint32_t i = 0; i < n; i++) {
			int r = za_check(&ht->arPacked[i], d + 1, ndim, mode, shape, seen);

			if (r != 0) return r;
		}
		return 0;
	}
	{
		/* the leaves: the smallest and largest type (IS_FALSE 2, IS_TRUE 3, IS_LONG 4, IS_DOUBLE 5) */
		const zval *a = ht->arPacked;
		uint8_t lo = 255, hi = 0;

		for (uint32_t i = 0; i < n; i++) {
			uint8_t t = Z_TYPE(a[i]);

			lo = t < lo ? t : lo;
			hi = t > hi ? t : hi;
		}
		if (lo < IS_FALSE || hi > IS_DOUBLE) return -1;
		if (lo <= IS_TRUE) *seen |= AS_BOOL;
		if (hi >= IS_LONG) {
			if (lo <= IS_TRUE) return -1;   /* bools and numbers */
			if (lo == IS_LONG) *seen |= AS_INT;
			if (hi == IS_DOUBLE) *seen |= AS_FLOAT;
		}
	}
	return 0;
}

/* the kind of what was seen; -1: bools and numbers */
static inline int za_kind(unsigned seen)
{
	seen &= 7u;
	if (!seen) return AK_EMPTY;
	if (seen & AS_BOOL) return seen == AS_BOOL ? AK_BOOL : -1;
	return seen == AS_INT ? AK_INT : seen == AS_FLOAT ? AK_FLOAT : AK_NUM;
}

/* the number of axes of an array: nested lists, by the first element at each level */
static inline int za_ndim(zval *z)
{
	int n = 0;

	while (Z_TYPE_P(z) == IS_ARRAY) {
		HashTable *ht = Z_ARRVAL_P(z);
		zval *first = NULL;

		n++;
		if (zend_hash_num_elements(ht) == 0) break;
		ZEND_HASH_FOREACH_VAL(ht, first) { break; } ZEND_HASH_FOREACH_END();
		z = first;
	}
	return n;
}

#endif
