/*
 * Copyright (c) 2011-2012 Red Hat, Inc.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
 *
 * All rights reserved.
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef ICMAP_H_DEFINED
#define ICMAP_H_DEFINED

#include <stdlib.h>
#include <corosync/corotypes.h>
#include <qb/qbmap.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum length of key in icmap
 */
#define ICMAP_KEYNAME_MAXLEN		255

/**
 * Minimum lenght of key in icmap
 */
#define ICMAP_KEYNAME_MINLEN		3

/**
 * Possible types of value. Binary is raw data without trailing zero with given length
 */
typedef enum {
    ICMAP_VALUETYPE_INT8	=  1,
    ICMAP_VALUETYPE_UINT8	=  2,
    ICMAP_VALUETYPE_INT16	=  3,
    ICMAP_VALUETYPE_UINT16	=  4,
    ICMAP_VALUETYPE_INT32	=  5,
    ICMAP_VALUETYPE_UINT32	=  6,
    ICMAP_VALUETYPE_INT64	=  7,
    ICMAP_VALUETYPE_UINT64	=  8,
    ICMAP_VALUETYPE_FLOAT	=  9,
    ICMAP_VALUETYPE_DOUBLE	= 10,
    ICMAP_VALUETYPE_STRING	= 11,
    ICMAP_VALUETYPE_BINARY	= 12,
} icmap_value_types_t;

/*
 * Tracking values.
 */
#define ICMAP_TRACK_ADD		4
#define ICMAP_TRACK_DELETE	1
#define ICMAP_TRACK_MODIFY	2

/**
 * Whole prefix is tracked, instead of key only (so "totem." tracking means that
 * "totem.nodeid", "totem.version", ... applies). This value is also never returned
 * inside of callback and is used only in adding track
 */
#define ICMAP_TRACK_PREFIX	8

/**
 * Structure passed as new_value and old_value in change callback. It contains type of
 * key, length of key and pointer to value of key
 */
struct icmap_notify_value {
	icmap_value_types_t type;
	size_t len;
	const void *data;
};

/**
 * Prototype for notify callback function. Even is one of ICMAP_TRACK_* event, key_name is
 * changed key, new and old_value contains values or are zeroed (in other words, type is non
 * existing 0 type) if there were no old (creating of key) or new (deleting of key) value.
 * user_data are passed when adding tracking.
 */
typedef void (*icmap_notify_fn_t) (
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_value,
	struct icmap_notify_value old_value,
	void *user_data);

/**
 * @brief icmap type.
 *
 * icmap.c contains global variable (icmap_global_map) of this type. This
 * is used in every non-reentant call. Also only in this table are implemented
 * operations like set_ro and tracking of values. Other tables (created by
 * icmap_init_r) are simple map tables with get/set/iter operations.
 */
typedef struct icmap_map *icmap_map_t;

/**
 * @brief Itterator type
 */
typedef qb_map_iter_t *icmap_iter_t;

/**
 * @brief Track type
 */
typedef struct icmap_track *icmap_track_t;

/**
 * @brief Initialize global icmap
 * @return
 */
extern cs_error_t icmap_init(void);

/**
 * @brief Initialize additional (local, reentrant) icmap_map. Content of variable
 * result is undefined if return code is not CS_OK.
 * @param result
 * @return
 */
extern cs_error_t icmap_init_r(icmap_map_t *result);

/**
 * @brief Finalize global icmap
 */
extern void icmap_fini(void);

/**
 * @brief Finalize local, reentrant icmap
 * @param map
 */
extern void icmap_fini_r(const icmap_map_t map);

/**
 * @brief Return global icmap
 * @return
 */
extern icmap_map_t icmap_get_global_map(void);

/**
 * @brief Compare value of key with name key_name1 in map1 with key with name key_name2
 * in map2.
 *
 * Two values must have same type, length and value to be considered equal.
 * Function returns 0 when any of map1, key_name1, map2, key_name2 are NULL, or
 * key_name is not found in map, or keys are not equal. != 0 is returned when
 * values are equal.
 *
 * @param map1
 * @param key_name1
 * @param map2
 * @param key_name2
 * @return
 */
extern int icmap_key_value_eq(
	const icmap_map_t map1,
	const char *key_name1,
	const icmap_map_t map2,
	const char *key_name2);

/**
 * @brief Store value with value_len length and type as key_name name in global icmap.
 * @param key_name
 * @param value
 * @param value_len
 * @param type
 * @return
 */
extern cs_error_t icmap_set(
	const char *key_name,
	const void *value,
	size_t value_len,
        icmap_value_types_t type);

/**
 * @brief Reentrant version of icmap_set
 * @param map
 * @param key_name
 * @param value
 * @param value_len
 * @param type
 * @return
 */
extern cs_error_t icmap_set_r(
	const icmap_map_t map,
	const char *key_name,
	const void *value,
	size_t value_len,
        icmap_value_types_t type);

/*
 * Shortcuts for setting values
 */
extern cs_error_t icmap_set_int8(const char *key_name, int8_t value);
extern cs_error_t icmap_set_uint8(const char *key_name, uint8_t value);
extern cs_error_t icmap_set_int16(const char *key_name, int16_t value);
extern cs_error_t icmap_set_uint16(const char *key_name, uint16_t value);
extern cs_error_t icmap_set_int32(const char *key_name, int32_t value);
extern cs_error_t icmap_set_uint32(const char *key_name, uint32_t value);
extern cs_error_t icmap_set_int64(const char *key_name, int64_t value);
extern cs_error_t icmap_set_uint64(const char *key_name, uint64_t value);
extern cs_error_t icmap_set_float(const char *key_name, float value);
extern cs_error_t icmap_set_double(const char *key_name, double value);
extern cs_error_t icmap_set_string(const char *key_name, const char *value);

extern cs_error_t icmap_set_int8_r(const icmap_map_t map, const char *key_name, int8_t value);
extern cs_error_t icmap_set_uint8_r(const icmap_map_t map, const char *key_name, uint8_t value);
extern cs_error_t icmap_set_int16_r(const icmap_map_t map, const char *key_name, int16_t value);
extern cs_error_t icmap_set_uint16_r(const icmap_map_t map, const char *key_name, uint16_t value);
extern cs_error_t icmap_set_int32_r(const icmap_map_t map, const char *key_name, int32_t value);
extern cs_error_t icmap_set_uint32_r(const icmap_map_t map, const char *key_name, uint32_t value);
extern cs_error_t icmap_set_int64_r(const icmap_map_t map, const char *key_name, int64_t value);
extern cs_error_t icmap_set_uint64_r(const icmap_map_t map, const char *key_name, uint64_t value);
extern cs_error_t icmap_set_float_r(const icmap_map_t map, const char *key_name, float value);
extern cs_error_t icmap_set_double_r(const icmap_map_t map, const char *key_name, double value);
extern cs_error_t icmap_set_string_r(const icmap_map_t map, const char *key_name, const char *value);

/**
 * @brief Delete key from map
 * @param key_name
 * @return
 */
extern cs_error_t icmap_delete(const char *key_name);

/**
 * @brief icmap_delete_r
 * @param map
 * @param key_name
 * @return
 */
extern cs_error_t icmap_delete_r(const icmap_map_t map, const char *key_name);

/**
 * @brief Retrieve value of key key_name and store it in user preallocated value pointer.
 *
 * Value can be NULL, and then only value_len and/or type is returned (both of them
 * can also be NULL). If value is not NULL, actual length of value in map is checked
 * against value_len. If *value_len is shorter then length of value in map, error
 * CS_ERR_INVALID_PARAM is returned. After successful copy of value, value_len is
 * set to actual length of value in map.
 *
 * @param key_name
 * @param value
 * @param value_len
 * @param type
 * @return
 */
extern cs_error_t icmap_get(
	const char *key_name,
	void *value,
	size_t *value_len,
        icmap_value_types_t *type);

/**
 * @brief Same as icmap_get but it's reentrant and operates on given icmap_map
 * @param map
 * @param key_name
 * @param value
 * @param value_len
 * @param type
 * @return
 */
extern cs_error_t icmap_get_r(
	const icmap_map_t map,
	const char *key_name,
	void *value,
	size_t *value_len,
	icmap_value_types_t *type);

/*
 * Shortcuts for icmap_get
 */
extern cs_error_t icmap_get_int8(const char *key_name, int8_t *i8);
extern cs_error_t icmap_get_uint8(const char *key_name, uint8_t *u8);
extern cs_error_t icmap_get_int16(const char *key_name, int16_t *i16);
extern cs_error_t icmap_get_uint16(const char *key_name, uint16_t *u16);
extern cs_error_t icmap_get_int32(const char *key_name, int32_t *i32);
extern cs_error_t icmap_get_uint32(const char *key_name, uint32_t *u32);
extern cs_error_t icmap_get_int64(const char *key_name, int64_t *i64);
extern cs_error_t icmap_get_uint64(const char *key_name, uint64_t *u64);
extern cs_error_t icmap_get_float(const char *key_name, float *flt);
extern cs_error_t icmap_get_double(const char *key_name, double *dbl);

/*
 * Shortcuts for icmap_get_r
 */
extern cs_error_t icmap_get_int8_r(const icmap_map_t map, const char *key_name, int8_t *i8);
extern cs_error_t icmap_get_uint8_r(const icmap_map_t map, const char *key_name, uint8_t *u8);
extern cs_error_t icmap_get_int16_r(const icmap_map_t map, const char *key_name, int16_t *i16);
extern cs_error_t icmap_get_uint16_r(const icmap_map_t map, const char *key_name, uint16_t *u16);
extern cs_error_t icmap_get_int32_r(const icmap_map_t map, const char *key_name, int32_t *i32);
extern cs_error_t icmap_get_uint32_r(const icmap_map_t map, const char *key_name, uint32_t *u32);
extern cs_error_t icmap_get_int64_r(const icmap_map_t map, const char *key_name, int64_t *i64);
extern cs_error_t icmap_get_uint64_r(const icmap_map_t map, const char *key_name, uint64_t *u64);
extern cs_error_t icmap_get_float_r(const icmap_map_t map, const char *key_name, float *flt);
extern cs_error_t icmap_get_double_r(const icmap_map_t map, const char *key_name, double *dbl);

/**
 * @brief Shortcut for icmap_get for string type.
 *
 * Returned string is newly allocated and
 * caller is responsible for freeing memory
 *
 * @param key_name
 * @param str
 * @return
 */
extern cs_error_t icmap_get_string(const char *key_name, char **str);

/**
 * @brief icmap_adjust_int
 *
 * Defined only for [u]int* values. It adds step to current value.
 *
 * @param key_name
 * @param step
 * @return
 */
extern cs_error_t icmap_adjust_int(const char *key_name, int32_t step);

/**
 * @brief icmap_adjust_int_r
 * @param map
 * @param key_name
 * @param step
 * @return
 */
extern cs_error_t icmap_adjust_int_r(const icmap_map_t map, const char *key_name, int32_t step);

/**
 * @brief icmap_fast_adjust_int
 *
 * Defined only for [u]int* values. It adds step to current value. Difference
 * between this function and icmap_adjust_int is given in fact, that in
 * tracking callback, old value is undefined, but whole process is done
 * without malloc/memcpy.
 *
 * @param key_name
 * @param step
 * @return
 */
extern cs_error_t icmap_fast_adjust_int(const char *key_name, int32_t step);

/**
 * @brief icmap_fast_adjust_int_r
 * @param map
 * @param key_name
 * @param step
 * @return
 */
extern cs_error_t icmap_fast_adjust_int_r(const icmap_map_t map, const char *key_name, int32_t step);

/**
 * @brief Increase stored value by one
 * @param key_name
 * @return
 */
extern cs_error_t icmap_inc(const char *key_name);

/**
 * @brief icmap_inc_r
 * @param map
 * @param key_name
 * @return
 */
extern cs_error_t icmap_inc_r(const icmap_map_t map, const char *key_name);

/**
 * @brief Decrease stored value by one
 * @param key_name
 * @return
 */
extern cs_error_t icmap_dec(const char *key_name);

/**
 * @brief icmap_dec_r
 * @param map
 * @param key_name
 * @return
 */
extern cs_error_t icmap_dec_r(const icmap_map_t map, const char *key_name);

/**
 * @brief Increase stored value by one.
 *
 * Difference between this function and icmap_inc
 * is same as between icmap_adjust_int and icmap_fast_adjust_int.
 *
 * @param key_name
 * @return
 */
extern cs_error_t icmap_fast_inc(const char *key_name);

/**
 * @brief icmap_fast_inc_r
 * @param map
 * @param key_name
 * @return
 */
extern cs_error_t icmap_fast_inc_r(const icmap_map_t map, const char *key_name);

/**
 * @brief Decrease stored value by one.
 *
 * Difference between this function and icmap_dec
 * is same as between icmap_adjust_int and icmap_fast_adjust_int.
 *
 * @param key_name
 * @return
 */
extern cs_error_t icmap_fast_dec(const char *key_name);

/**
 * @brief icmap_fast_dec_r
 * @param map
 * @param key_name
 * @return
 */
extern cs_error_t icmap_fast_dec_r(const icmap_map_t map, const char *key_name);

/**
 * @brief Initialize iterator with given prefix
 * @param prefix
 * @return
 */
extern icmap_iter_t icmap_iter_init(const char *prefix);

/**
 * @brief icmap_iter_init_r
 * @param map
 * @param prefix
 * @return
 */
extern icmap_iter_t icmap_iter_init_r(const icmap_map_t map, const char *prefix);

/**
 * @brief Return next item in iterator iter.
 *
 * value_len and type are optional (= can be NULL), but if set, length of returned value
 * and/or type is returned. Function returns following key_name or NULL if iteration is over.
 *
 * @param iter
 * @param value_len
 * @param type
 * @return
 */
extern const char *icmap_iter_next(icmap_iter_t iter, size_t *value_len, icmap_value_types_t *type);

/**
 * @brief Finalize iterator
 * @param iter
 */
extern void icmap_iter_finalize(icmap_iter_t iter);

/**
 * @brief Add tracking function for given key_name.
 *
 * Tracked changes (add|modify|delete) depend on track_type, which is bitwise or of ICMAP_TRACK_* values.
 * notify_fn is called on change, where user_data pointer is passed (unchanged).
 * Value which can be used to delete tracking is passed as icmap_track.
 *
 * @param key_name
 * @param track_type
 * @param notify_fn
 * @param user_data
 * @param icmap_track
 * @return
 */
extern cs_error_t icmap_track_add(
	const char *key_name,
	int32_t track_type,
	icmap_notify_fn_t notify_fn,
	void *user_data,
	icmap_track_t *icmap_track);

/**
 * @brief Return user data associated with given track
 * @param icmap_track
 * @return
 */
extern void *icmap_track_get_user_data(icmap_track_t icmap_track);

/**
 * @brief Remove previously added track
 * @param icmap_track
 * @return
 */
extern cs_error_t icmap_track_delete(icmap_track_t icmap_track);

/**
 * @brief Set read-only access for given key (key_name) or prefix,
 * If prefix is set. ro_access can be !0, which means, that old information
 * about ro of this key is deleted. Read-only access is used only in CMAP service!
 * (in other word it prevents users from deleting/changing key, but doesn't
 *  guarantee anything for internal icmap users.)
 * @param key_name
 * @param prefix
 * @param ro_access
 * @return
 */
extern cs_error_t icmap_set_ro_access(const char *key_name, int prefix, int ro_access);

/**
 * @brief Check in given key is read only. Returns !0 if so, otherwise (key is rw) 0.
 * @param key_name
 * @return
 */
extern int icmap_is_key_ro(const char *key_name);

/**
 * @brief Converts given key_name to valid key name (replacing all prohibited characters by _)
 * @param key_name
 */
extern void icmap_convert_name_to_valid_name(char *key_name);

/**
 * @brief Copy content of src_map icmap to dst_map icmap.
 * @param dst_map
 * @param src_map
 * @return
 */
extern cs_error_t icmap_copy_map(icmap_map_t dst_map, const icmap_map_t src_map);

#ifdef __cplusplus
}
#endif

#endif /* ICMAP_H_DEFINED */
