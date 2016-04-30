/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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

#ifndef COROSYNC_CMAP_H_DEFINED
#define COROSYNC_CMAP_H_DEFINED

#include <corosync/corotypes.h>
#include <corosync/hdb.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup cmap_corosync
 *
 * @{
 */

/*
 * Handle for cmap service connection
 */
typedef uint64_t cmap_handle_t;

/*
 * Handle for cmap iterator
 */
typedef uint64_t cmap_iter_handle_t;

/*
 * Handle for cmap tracking function
 */
typedef uint64_t cmap_track_handle_t;

/*
 * Maximum length of key in cmap
 */
#define CMAP_KEYNAME_MAXLEN            255
/*
 * Minumum length of key in cmap
 */
#define CMAP_KEYNAME_MINLEN            3

/*
 * Tracking values.
 */
#define CMAP_TRACK_ADD		4
#define CMAP_TRACK_DELETE	1
#define CMAP_TRACK_MODIFY	2

/**
 * Whole prefix is tracked, instead of key only (so "totem." tracking means that
 * "totem.nodeid", "totem.version", ... applies). This value is also never returned
 * inside of callback and is used only in adding track
 */
#define CMAP_TRACK_PREFIX	8

/**
 * Possible types of value. Binary is raw data without trailing zero with given length
 */
typedef enum {
    CMAP_VALUETYPE_INT8 	=  1,
    CMAP_VALUETYPE_UINT8	=  2,
    CMAP_VALUETYPE_INT16	=  3,
    CMAP_VALUETYPE_UINT16	=  4,
    CMAP_VALUETYPE_INT32	=  5,
    CMAP_VALUETYPE_UINT32	=  6,
    CMAP_VALUETYPE_INT64	=  7,
    CMAP_VALUETYPE_UINT64	=  8,
    CMAP_VALUETYPE_FLOAT	=  9,
    CMAP_VALUETYPE_DOUBLE	= 10,
    CMAP_VALUETYPE_STRING	= 11,
    CMAP_VALUETYPE_BINARY	= 12,
} cmap_value_types_t;

/**
 * Structure passed as new_value and old_value in change callback. It contains type of
 * key, length of key and pointer to value of key
 */
struct cmap_notify_value {
	cmap_value_types_t type;
	size_t len;
	const void *data;
};

/**
 * Prototype for notify callback function. Even is one of CMAP_TRACK_* event, key_name is
 * changed key, new and old_value contains values or are zeroed (in other words, type is non
 * existing 0 type) if there were no old (creating of key) or new (deleting of key) value.
 * user_data are passed when adding tracking.
 */
typedef void (*cmap_notify_fn_t) (
	cmap_handle_t cmap_handle,
	cmap_track_handle_t cmap_track_handle,
	int32_t event,
	const char *key_name,
	struct cmap_notify_value new_value,
	struct cmap_notify_value old_value,
	void *user_data);

/**
 * Create a new cmap connection
 *
 * @param handle will be filled with handle to be used for all following
 * operations with cht.
 */
extern cs_error_t cmap_initialize (
	cmap_handle_t *handle);

/**
 * Close the cmap handle
 * @param handle cmap handle
 */
extern cs_error_t cmap_finalize (
	cmap_handle_t handle);

/**
 * Get a file descriptor on which to poll.  cmap_handle_t is NOT a
 * file descriptor and may not be used directly.
 * @param handle cmap handle initialized by cmap_initialize
 * @param fd file descriptor for poll
 */
extern cs_error_t cmap_fd_get (
	cmap_handle_t handle,
	int *fd);

/**
 * Dispatch data from service.
 * @param handle cmap handle initialized by cmap_initialize
 * @param dispatch_types one of standard corosync dispatch values
 */
extern cs_error_t cmap_dispatch (
	cmap_handle_t handle,
	cs_dispatch_flags_t dispatch_types);
/**
 * @brief cmap_context_get
 * @param handle
 * @param context
 * @return
 */
extern cs_error_t cmap_context_get (
	cmap_handle_t handle,
	const void **context);

/**
 * @brief cmap_context_set
 * @param handle
 * @param context
 * @return
 */
extern cs_error_t cmap_context_set (
	cmap_handle_t handle,
	const void *context);

/**
 * Store value in cmap
 * @param handle cmap handle
 * @param key_name name of key where to store value
 * @param value value to store
 * @param value_len length of value to store
 * @param type type to store
 */
extern cs_error_t cmap_set(
	cmap_handle_t handle,
	const char *key_name,
	const void *value,
	size_t value_len,
	cmap_value_types_t type);

/*
 * Shortcuts for cmap_set with given type
 */
extern cs_error_t cmap_set_int8(cmap_handle_t handle, const char *key_name, int8_t value);
extern cs_error_t cmap_set_uint8(cmap_handle_t handle, const char *key_name, uint8_t value);
extern cs_error_t cmap_set_int16(cmap_handle_t handle, const char *key_name, int16_t value);
extern cs_error_t cmap_set_uint16(cmap_handle_t handle, const char *key_name, uint16_t value);
extern cs_error_t cmap_set_int32(cmap_handle_t handle, const char *key_name, int32_t value);
extern cs_error_t cmap_set_uint32(cmap_handle_t handle, const char *key_name, uint32_t value);
extern cs_error_t cmap_set_int64(cmap_handle_t handle, const char *key_name, int64_t value);
extern cs_error_t cmap_set_uint64(cmap_handle_t handle, const char *key_name, uint64_t value);
extern cs_error_t cmap_set_float(cmap_handle_t handle, const char *key_name, float value);
extern cs_error_t cmap_set_double(cmap_handle_t handle, const char *key_name, double value);
extern cs_error_t cmap_set_string(cmap_handle_t handle, const char *key_name, const char *value);

/**
 * Deletes key from cmap database
 * @param handle cmap handle
 * @param key_name name of key to delete
 */
extern cs_error_t cmap_delete(cmap_handle_t handle, const char *key_name);

/**
 * @brief Retrieve value of key key_name and store it in user preallocated value pointer.
 *
 * value can be NULL, and then only value_len and/or type is returned (both of them
 * can also be NULL). If value is not NULL, actual length of value in map is checked
 * against value_len. If *value_len is shorter then length of value in map, error
 * CS_ERR_INVALID_PARAM is returned. After successful copy of value, value_len is
 * set to actual length of value in map.
 *
 * @param handle cmap handle
 * @param key_name name of key where to get value
 * @param value pointer to store data (or NULL)
 * @param value_len pointer with length of value (value != NULL), or pointer where value length
 * will be returned (value == NULL) or NULL.
 * @param type type of value in cmap
 */
extern cs_error_t cmap_get(
	cmap_handle_t handle,
	const char *key_name,
	void *value,
	size_t *value_len,
	cmap_value_types_t *type);

/*
 * Shortcuts for cmap_get.
 */
extern cs_error_t cmap_get_int8(cmap_handle_t handle, const char *key_name, int8_t *i8);
extern cs_error_t cmap_get_uint8(cmap_handle_t handle, const char *key_name, uint8_t *u8);
extern cs_error_t cmap_get_int16(cmap_handle_t handle, const char *key_name, int16_t *i16);
extern cs_error_t cmap_get_uint16(cmap_handle_t handle, const char *key_name, uint16_t *u16);
extern cs_error_t cmap_get_int32(cmap_handle_t handle, const char *key_name, int32_t *i32);
extern cs_error_t cmap_get_uint32(cmap_handle_t handle, const char *key_name, uint32_t *u32);
extern cs_error_t cmap_get_int64(cmap_handle_t handle, const char *key_name, int64_t *i64);
extern cs_error_t cmap_get_uint64(cmap_handle_t handle, const char *key_name, uint64_t *u64);
extern cs_error_t cmap_get_float(cmap_handle_t handle, const char *key_name, float *flt);
extern cs_error_t cmap_get_double(cmap_handle_t handle, const char *key_name, double *dbl);

/**
 * @brief Shortcut for cmap_get for string type.
 *
 * Returned string is newly allocated and caller is responsible for freeing memory
 *
 * @param handle cmap handle
 * @param key_name name of key to get value from
 * @param str pointer where char pointer will be stored
 */
extern cs_error_t cmap_get_string(cmap_handle_t handle, const char *key_name, char **str);

/**
 * @brief Increment value of key_name if it is [u]int* type
 *
 * @param handle cmap handle
 * @param key_name key name
 */
extern cs_error_t cmap_inc(cmap_handle_t handle, const char *key_name);

/**
 * @brief Decrement value of key_name if it is [u]int* type
 *
 * @param handle cmap handle
 * @param key_name key name
 */
extern cs_error_t cmap_dec(cmap_handle_t handle, const char *key_name);

/**
 * @brief Initialize iterator with given prefix
 *
 * @param handle cmap handle
 * @param prefix prefix to iterate on
 * @param cmap_iter_handle value used for getting next value of iterator and/or deleting iteration
 */
extern cs_error_t cmap_iter_init(cmap_handle_t handle, const char *prefix, cmap_iter_handle_t *cmap_iter_handle);

/**
 * @brief Return next item in iterator iter.
 *
 * value_len and type are optional (= can be NULL), but if set,
 * length of returned value and/or type is returned.
 *
 * @param handle cmap handle
 * @param iter_handle handle of iteration returned by cmap_iter_init
 * @param key_name place to store name of key. Maximum length is CMAP_KEYNAME_MAXLEN
 * @param value_len length of value
 * @param type type of value
 * @return CS_NO_SECTION if there are no more sections to iterate
 */
extern cs_error_t cmap_iter_next(
		cmap_handle_t handle,
		cmap_iter_handle_t iter_handle,
		char key_name[],
		size_t *value_len,
		cmap_value_types_t *type);

/**
 * @brief Finalize iterator
 * @param handle
 * @param iter_handle
 * @return
 */
extern cs_error_t cmap_iter_finalize(cmap_handle_t handle, cmap_iter_handle_t iter_handle);

/**
 * @brief Add tracking function for given key_name.
 *
 * Tracked changes (add|modify|delete) depend on track_type,
 * which is bitwise or of CMAP_TRACK_* values. notify_fn is called on change, where user_data pointer
 * is passed (unchanged). Value which can be used to delete tracking is passed as cmap_track.
 *
 * @param handle cmap handle
 * @param key_name name of key to track changes on
 * @param track_type bitwise-or of CMAP_TRACK_* values
 * @param notify_fn function to be called on change of key
 * @param user_data given pointer is unchanged passed to notify_fn
 * @param cmap_track_handle handle used for removing of newly created track
 */
extern cs_error_t cmap_track_add(
	cmap_handle_t handle,
	const char *key_name,
        int32_t track_type,
	cmap_notify_fn_t notify_fn,
        void *user_data,
        cmap_track_handle_t *cmap_track_handle);

/**
 * Delete track created previously by cmap_track_add
 * @param handle cmap handle
 * @param track_handle Track handle
 */
extern cs_error_t cmap_track_delete(cmap_handle_t handle, cmap_track_handle_t track_handle);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_CMAP_H_DEFINED */
