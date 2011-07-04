/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
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
#ifndef COROAPI_H_DEFINED
#define COROAPI_H_DEFINED

#include <stdio.h>
#ifdef COROSYNC_BSD
#include <sys/uio.h>
#endif
#include <corosync/hdb.h>
#include <corosync/swab.h>

typedef struct {
	uint32_t nodeid __attribute__((aligned(8)));
	void *conn __attribute__((aligned(8)));
} mar_message_source_t __attribute__((aligned(8)));

static inline void swab_mar_message_source_t (mar_message_source_t *to_swab)
{
	swab32 (to_swab->nodeid);
	/*
	 * if it is from a byteswapped machine, then we can safely
	 * ignore its conn info data structure since this is only
	 * local to the machine
	 */
	to_swab->conn = NULL;
}
typedef void * corosync_timer_handle_t;

struct corosync_tpg_group {
	const void *group;
	size_t group_len;
};

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

#define INTERFACE_MAX 2

#ifdef HAVE_SMALL_MEMORY_FOOTPRINT
#define PROCESSOR_COUNT_MAX	16
#define MESSAGE_SIZE_MAX	1024*64
#define MESSAGE_QUEUE_MAX	512
#else
#define PROCESSOR_COUNT_MAX	384
#define MESSAGE_SIZE_MAX	1024*1024 /* (1MB) */
#define MESSAGE_QUEUE_MAX	MESSAGE_SIZE_MAX / totem_config->net_mtu
#endif /* HAVE_SMALL_MEMORY_FOOTPRINT */

#define TOTEM_AGREED	0
#define TOTEM_SAFE	1

#define MILLI_2_NANO_SECONDS 1000000ULL

#if !defined(TOTEM_IP_ADDRESS)
struct totem_ip_address {
	unsigned int   nodeid;
	unsigned short family;
	unsigned char  addr[TOTEMIP_ADDRLEN];
} __attribute__((packed));
#endif

#if !defined(MEMB_RING_ID)
struct memb_ring_id {
	struct totem_ip_address rep;
	unsigned long long seq;
} __attribute__((packed));
#endif

#if !defined(TOTEM_CONFIGURATION_TYPE)
enum totem_configuration_type {
	TOTEM_CONFIGURATION_REGULAR,
	TOTEM_CONFIGURATION_TRANSITIONAL
};
#endif

#if !defined(TOTEM_CALLBACK_TOKEN_TYPE)
enum totem_callback_token_type {
	TOTEM_CALLBACK_TOKEN_RECEIVED = 1,
	TOTEM_CALLBACK_TOKEN_SENT = 2
};
#endif

enum cs_lib_flow_control {
	CS_LIB_FLOW_CONTROL_REQUIRED = 1,
	CS_LIB_FLOW_CONTROL_NOT_REQUIRED = 2
};
#define corosync_lib_flow_control cs_lib_flow_control
#define COROSYNC_LIB_FLOW_CONTROL_REQUIRED CS_LIB_FLOW_CONTROL_REQUIRED
#define COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED CS_LIB_FLOW_CONTROL_NOT_REQUIRED

enum cs_lib_allow_inquorate {
	CS_LIB_DISALLOW_INQUORATE = 0, /* default */
	CS_LIB_ALLOW_INQUORATE = 1
};

#if !defined (COROSYNC_FLOW_CONTROL_STATE)
enum cs_flow_control_state {
	CS_FLOW_CONTROL_STATE_DISABLED,
	CS_FLOW_CONTROL_STATE_ENABLED
};
#define corosync_flow_control_state cs_flow_control_state
#define CS_FLOW_CONTROL_STATE_DISABLED CS_FLOW_CONTROL_STATE_DISABLED
#define CS_FLOW_CONTROL_STATE_ENABLED CS_FLOW_CONTROL_STATE_ENABLED

#endif /* COROSYNC_FLOW_CONTROL_STATE */

enum cs_sync_mode {
	CS_SYNC_V1	 = 0,
	CS_SYNC_V2	 = 1,
	CS_SYNC_V1_APIV2 = 2
};

typedef enum {
	COROSYNC_FATAL_ERROR_EXIT = -1,
	COROSYNC_LIBAIS_SOCKET = -6,
	COROSYNC_LIBAIS_BIND = -7,
	COROSYNC_READKEY = -8,
	COROSYNC_INVALID_CONFIG = -9,
	COROSYNC_DYNAMICLOAD = -12,
	COROSYNC_OUT_OF_MEMORY = -15,
	COROSYNC_FATAL_ERR = -16
} cs_fatal_error_t;
#define corosync_fatal_error_t cs_fatal_error_t;

#ifndef OBJECT_PARENT_HANDLE

#define OBJECT_PARENT_HANDLE 0xffffffff00000000ULL

struct object_valid {
	char *object_name;
	size_t object_len;
};

struct object_key_valid {
	char *key_name;
	size_t key_len;
	int (*validate_callback) (const void *key, size_t key_len,
				  const void *value, size_t value_len);
};
/* deprecated */

typedef enum {
	OBJDB_VALUETYPE_INT16,
	OBJDB_VALUETYPE_UINT16,
	OBJDB_VALUETYPE_INT32,
	OBJDB_VALUETYPE_UINT32,
	OBJDB_VALUETYPE_INT64,
	OBJDB_VALUETYPE_UINT64,
	OBJDB_VALUETYPE_FLOAT,
	OBJDB_VALUETYPE_DOUBLE,
	OBJDB_VALUETYPE_STRING,
	OBJDB_VALUETYPE_ANY,
} objdb_value_types_t;

typedef enum {
	OBJECT_TRACK_DEPTH_ONE,
	OBJECT_TRACK_DEPTH_RECURSIVE
} object_track_depth_t;

typedef enum {
	OBJECT_KEY_CREATED,
	OBJECT_KEY_REPLACED,
	OBJECT_KEY_DELETED
} object_change_type_t;

typedef enum {
        OBJDB_RELOAD_NOTIFY_START,
        OBJDB_RELOAD_NOTIFY_END,
	OBJDB_RELOAD_NOTIFY_FAILED
} objdb_reload_notify_type_t;

typedef void (*object_key_change_notify_fn_t)(
	object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt);

typedef void (*object_create_notify_fn_t) (
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt);

typedef void (*object_destroy_notify_fn_t) (
	hdb_handle_t parent_object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt);

typedef void (*object_notify_callback_fn_t)(
	hdb_handle_t object_handle,
	const void *key_name, size_t key_len,
	const void *value, size_t value_len,
	object_change_type_t type,
	const void * priv_data_pt);

typedef void (*object_reload_notify_fn_t) (
	objdb_reload_notify_type_t,
	int flush,
	void *priv_data_pt);

#endif /* OBJECT_PARENT_HANDLE_DEFINED */

#ifndef QUORUM_H_DEFINED
typedef void (*quorum_callback_fn_t) (int quorate, void *context);

struct quorum_callin_functions
{
	int (*quorate) (void);
	int (*register_callback) (quorum_callback_fn_t callback_fn, void *context);
	int (*unregister_callback) (quorum_callback_fn_t callback_fn, void *context);
};

typedef void (*sync_callback_fn_t) (
	const unsigned int *view_list,
	size_t view_list_entries,
	int primary_designated,
	struct memb_ring_id *ring_id);

#endif /* QUORUM_H_DEFINED */

struct corosync_api_v1 {
	/*
	 * Object and configuration APIs
	 */
	int (*object_create) (
		hdb_handle_t parent_object_handle,
		hdb_handle_t *object_handle,
		const void *object_name,
		size_t object_name_len);

	int (*object_priv_set) (
		hdb_handle_t object_handle,
		void *priv);

	int (*object_key_create) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		const void *value,
		size_t value_len);

	int (*object_destroy) (
		hdb_handle_t object_handle);

	int (*object_valid_set) (
		hdb_handle_t object_handle,
		struct object_valid *object_valid_list,
		size_t object_valid_list_entries);

	int (*object_key_valid_set) (
		hdb_handle_t object_handle,
		struct object_key_valid *object_key_valid_list,
		size_t object_key_valid_list_entries);

	int (*object_find_create) (
		hdb_handle_t parent_object_handle,
		const void *object_name,
		size_t object_name_len,
		hdb_handle_t *object_find_handle);

	int (*object_find_next) (
		hdb_handle_t object_find_handle,
		hdb_handle_t *object_handle);

	int (*object_find_destroy) (
		hdb_handle_t object_find_handle);

	int (*object_key_get) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		void **value,
		size_t *value_len);

	int (*object_priv_get) (
		hdb_handle_t jobject_handle,
		void **priv);

	int (*object_key_replace) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		const void *new_value,
		size_t new_value_len);

	int (*object_key_delete) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len);

	int (*object_iter_reset) (
		hdb_handle_t parent_object_handle);

	int (*object_iter) (
		hdb_handle_t parent_object_handle,
		void **object_name,
		size_t *name_len,
		hdb_handle_t *object_handle);

	int (*object_key_iter_reset) (
		hdb_handle_t object_handle);

	int (*object_key_iter) (
		hdb_handle_t parent_object_handle,
		void **key_name,
		size_t *key_len,
		void **value,
		size_t *value_len);

	int (*object_parent_get) (
		hdb_handle_t object_handle,
		hdb_handle_t *parent_handle);

	int (*object_name_get) (
		hdb_handle_t object_handle,
		char *object_name,
		size_t *object_name_len);

	int (*object_dump) (
		hdb_handle_t object_handle,
		FILE *file);

	int (*object_key_iter_from) (
		hdb_handle_t parent_object_handle,
		hdb_handle_t start_pos,
		void **key_name,
		size_t *key_len,
		void **value,
		size_t *value_len);

	int (*object_track_start) (
		hdb_handle_t object_handle,
		object_track_depth_t depth,
		object_key_change_notify_fn_t key_change_notify_fn,
		object_create_notify_fn_t object_create_notify_fn,
		object_destroy_notify_fn_t object_destroy_notify_fn,
		object_reload_notify_fn_t object_reload_notify_fn,
		void * priv_data_pt);

	void (*object_track_stop) (
		object_key_change_notify_fn_t key_change_notify_fn,
		object_create_notify_fn_t object_create_notify_fn,
		object_destroy_notify_fn_t object_destroy_notify_fn,
		object_reload_notify_fn_t object_reload_notify_fn,
		void * priv_data_pt);

	int (*object_write_config) (const char **error_string);

	int (*object_reload_config) (int flush,
				     const char **error_string);

	int (*object_key_increment) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		unsigned int *value);

	int (*object_key_decrement) (
		hdb_handle_t object_handle,
		const void *key_name,
		size_t key_len,
		unsigned int *value);

	/*
	 * Time and timer APIs
	 */
	int (*timer_add_duration) (
		unsigned long long nanoseconds_in_future,
		void *data,
		void (*timer_nf) (void *data),
		corosync_timer_handle_t *handle);

	int (*timer_add_absolute) (
		unsigned long long nanoseconds_from_epoch,
		void *data,
		void (*timer_fn) (void *data),
		corosync_timer_handle_t *handle);

	void (*timer_delete) (
		corosync_timer_handle_t timer_handle);

	unsigned long long (*timer_time_get) (void);

	unsigned long long (*timer_expire_time_get) (
		corosync_timer_handle_t timer_handle);

	/*
	 * IPC APIs
	 */
	void (*ipc_source_set) (mar_message_source_t *source, void *conn);

	int (*ipc_source_is_local) (const mar_message_source_t *source);

	void *(*ipc_private_data_get) (void *conn);

	int (*ipc_response_send) (void *conn, const void *msg, size_t mlen);

	int (*ipc_response_iov_send) (void *conn,
				      const struct iovec *iov, unsigned int iov_len);

	int (*ipc_dispatch_send) (void *conn, const void *msg, size_t mlen);

	int (*ipc_dispatch_iov_send) (void *conn,
				      const struct iovec *iov, unsigned int iov_len);

	void (*ipc_refcnt_inc) (void *conn);

	void (*ipc_refcnt_dec) (void *conn);

	/*
	 * Totem APIs
	 */
	unsigned int (*totem_nodeid_get) (void);

	int (*totem_family_get) (void);

	int (*totem_ring_reenable) (void);

	int (*totem_mcast) (const struct iovec *iovec,
			    unsigned int iov_len, unsigned int guarantee);

	int (*totem_ifaces_get) (
		unsigned int nodeid,
		struct totem_ip_address *interfaces,
		char ***status,
		unsigned int *iface_count);

	const char *(*totem_ifaces_print) (unsigned int nodeid);

	const char *(*totem_ip_print) (const struct totem_ip_address *addr);

	int (*totem_crypto_set) (unsigned int type);

	int (*totem_callback_token_create) (
		void **handle_out,
		enum totem_callback_token_type type,
		int delete,
		int (*callback_fn) (enum totem_callback_token_type type,
				    const void *),
		const void *data);

	/*
	 * Totem open process groups API for those service engines
	 * wanting their own groups
	 */
	int (*tpg_init) (
		hdb_handle_t *handle,

		void (*deliver_fn) (
			unsigned int nodeid,
			const void *msg,
			unsigned int msg_len,
			int endian_conversion_required),

		void (*confchg_fn) (
			enum totem_configuration_type configuration_type,
			const unsigned int *member_list,
			size_t member_list_entries,
			const unsigned int *left_list,
			size_t left_list_entries,
			const unsigned int *joined_list,
			size_t joined_list_entries,
			const struct memb_ring_id *ring_id));

	int (*tpg_exit) (
       		hdb_handle_t handle);

	int (*tpg_join) (
		hdb_handle_t handle,
		const struct corosync_tpg_group *groups,
		size_t group_cnt);

	int (*tpg_leave) (
		hdb_handle_t handle,
		const struct corosync_tpg_group *groups,
		size_t group_cnt);

	int (*tpg_joined_mcast) (
		hdb_handle_t handle,
		const struct iovec *iovec,
		unsigned int iov_len,
		int guarantee);

	int (*tpg_joined_reserve) (
		hdb_handle_t handle,
		const struct iovec *iovec,
		unsigned int iov_len);

	int (*tpg_joined_release) (
		int reserved_msgs);

	int (*tpg_groups_mcast) (
		hdb_handle_t handle,
		int guarantee,
		const struct corosync_tpg_group *groups,
		size_t groups_cnt,
		const struct iovec *iovec,
		unsigned int iov_len);

	int (*tpg_groups_reserve) (
		hdb_handle_t handle,
		const struct corosync_tpg_group *groups,
		size_t groups_cnt,
		const struct iovec *iovec,
		unsigned int iov_len);

	int (*tpg_groups_release) (
		int reserved_msgs);

	int (*schedwrk_create) (
		hdb_handle_t *handle,
		int (schedwrk_fn) (const void *),
		const void *context);

	void (*schedwrk_destroy) (hdb_handle_t handle);

	int (*sync_request) (
		const char *service_name);

	/*
	 * User plugin-callable functions for quorum
	 */
	int (*quorum_is_quorate) (void);
	int (*quorum_register_callback) (quorum_callback_fn_t callback_fn, void *context);
	int (*quorum_unregister_callback) (quorum_callback_fn_t callback_fn, void *context);

	/*
	 * This one is for the quorum management plugin's use
	 */
	int (*quorum_initialize)(struct quorum_callin_functions *fns);

	/*
	 * Plugin loading and unloading
	 */
	int (*plugin_interface_reference) (
		hdb_handle_t *handle,
		const char *iface_name,
		int version,
		void **interface,
		void *context);

	int (*plugin_interface_release) (hdb_handle_t handle);

	/*
	 * Service loading and unloading APIs
	*/
	unsigned int (*service_link_and_init) (
		struct corosync_api_v1 *corosync_api_v1,
		const char *service_name,
		unsigned int service_ver);

	unsigned int (*service_unlink_and_exit) (
		struct corosync_api_v1 *corosync_api_v1,
		const char *service_name,
		unsigned int service_ver);

	/*
	 * Error handling APIs
	 */
	void (*error_memory_failure) (void) __attribute__ ((noreturn));

#define corosync_fatal_error(err) api->fatal_error ((err), __FILE__, __LINE__)
	void (*fatal_error) (cs_fatal_error_t err,
		const char *file,
		unsigned int line) __attribute__ ((noreturn));

	void (*shutdown_request) (void);

	void (*state_dump) (void);

	/*
	 * The use of this interface is highly discouraged.
	 * Please avoid using any of coropoll apis in your service engines.
	 */
	hdb_handle_t (*poll_handle_get) (void);

	int (*object_key_create_typed) (
		hdb_handle_t object_handle,
		const char *key_name,
		const void *value,
		size_t value_len,
		objdb_value_types_t type);

	int (*object_key_get_typed) (
		hdb_handle_t object_handle,
		const char *key_name,
		void **value,
		size_t *value_len,
		objdb_value_types_t *type);

	int (*object_key_iter_typed) (
		hdb_handle_t parent_object_handle,
		char **key_name,
		void **value,
		size_t *value_len,
		objdb_value_types_t *type);

	void *(*totem_get_stats)(void);

	int (*schedwrk_create_nolock) (
		hdb_handle_t *handle,
		int (schedwrk_fn) (const void *),
		const void *context);

	int (*poll_dispatch_add) (hdb_handle_t handle,
		int fd,
		int events,
		void *data,

		int (*dispatch_fn) (hdb_handle_t handle,
			int fd,
			int revents,
			void *data));


	int (*poll_dispatch_delete) (
		hdb_handle_t handle,
		int fd);

};

#define SERVICE_ID_MAKE(a,b) ( ((a)<<16) | (b) )

#define SERVICE_HANDLER_MAXIMUM_COUNT 64

struct corosync_lib_handler {
	void (*lib_handler_fn) (void *conn, const void *msg);
	enum cs_lib_flow_control flow_control;
};

struct corosync_exec_handler {
	void (*exec_handler_fn) (const void *msg, unsigned int nodeid);
	void (*exec_endian_convert_fn) (void *msg);
};

struct corosync_service_engine_iface_ver0 {
        struct corosync_service_engine *(*corosync_get_service_engine_ver0) (void);
};

typedef void (*sync_init_v1_fn_t) (
		const unsigned int *member_list,
		size_t member_list_entries,
		const struct memb_ring_id *ring_id) ;

struct corosync_service_engine {
	const char *name;
	unsigned short id;
	unsigned short priority; /* Lower priority are loaded first, unloaded last.
				  * 0 is a special case which always loaded _and_ unloaded last
				  */
	unsigned int private_data_size;
	enum cs_lib_flow_control flow_control;
	enum cs_lib_allow_inquorate allow_inquorate;
	int (*exec_init_fn) (struct corosync_api_v1 *);
	int (*exec_exit_fn) (void);
	void (*exec_dump_fn) (void);
	int (*lib_init_fn) (void *conn);
	int (*lib_exit_fn) (void *conn);
	struct corosync_lib_handler *lib_engine;
	int lib_engine_count;
	struct corosync_exec_handler *exec_engine;
	int exec_engine_count;
	int (*config_init_fn) (struct corosync_api_v1 *);
	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		const unsigned int *member_list, size_t member_list_entries,
		const unsigned int *left_list, size_t left_list_entries,
		const unsigned int *joined_list, size_t joined_list_entries,
		const struct memb_ring_id *ring_id);
	enum cs_sync_mode sync_mode;
	sync_init_v1_fn_t sync_init;
	int (*sync_process) (void);
	void (*sync_activate) (void);
	void (*sync_abort) (void);
};

#endif /* COROAPI_H_DEFINED */
