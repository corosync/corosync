/*
 * Copyright (c) 2008 Red Hat, Inc.
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

typedef void * corosync_timer_handle_t;

typedef unsigned int cs_tpg_handle;

struct corosync_tpg_group {
	void *group;
	int group_len;
};

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

#define PROCESSOR_COUNT_MAX 384
#define INTERFACE_MAX 2

#ifndef MESSAGE_SIZE_MAX
#define MESSAGE_SIZE_MAX	1024*1024 /* (1MB) */
#endif /* MESSAGE_SIZE_MAX */

#ifndef MESSAGE_QUEUE_MAX
#define MESSAGE_QUEUE_MAX MESSAGE_SIZE_MAX / totem_config->net_mtu
#endif /* MESSAGE_QUEUE_MAX */

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

#define OBJECT_PARENT_HANDLE 0

struct object_valid {
	char *object_name;
	int object_len;
};

struct object_key_valid {
	char *key_name;
	int key_len;
	int (*validate_callback) (void *key, int key_len, void *value, int value_len);
};
/* deprecated */

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
        OBJDB_RELOAD_NOTIFY_END
} objdb_reload_notify_type_t;

typedef void (*object_key_change_notify_fn_t)(object_change_type_t change_type,
											  unsigned int parent_object_handle,
											  unsigned int object_handle,
											  void *object_name_pt, int object_name_len,
											  void *key_name_pt, int key_len,
											  void *key_value_pt, int key_value_len,
											  void *priv_data_pt);

typedef void (*object_create_notify_fn_t) (unsigned int parent_object_handle,
										   unsigned int object_handle,
										   uint8_t *name_pt, int name_len,
										   void *priv_data_pt);

typedef void (*object_destroy_notify_fn_t) (unsigned int parent_object_handle,
											uint8_t *name_pt, int name_len,
											void *priv_data_pt);
typedef void (*object_notify_callback_fn_t)(unsigned int object_handle,
											void *key_name, int key_len,
											void *value, int value_len,
											object_change_type_t type,
											void * priv_data_pt);

typedef void (*object_reload_notify_fn_t) (objdb_reload_notify_type_t, int flush,
											void *priv_data_pt);


#endif /* OBJECT_PARENT_HANDLE_DEFINED */

struct corosync_api_v1 {
	/*
	 * Object and configuration APIs
	 */
	int (*object_create) (
		unsigned int parent_object_handle,
		unsigned int *object_handle,
		void *object_name, unsigned int object_name_len);

	int (*object_priv_set) (
		unsigned int object_handle,
		void *priv);

	int (*object_key_create) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void *value,
		int value_len);

	int (*object_destroy) (
		unsigned int object_handle);

	int (*object_valid_set) (
		unsigned int object_handle,
		struct object_valid *object_valid_list,
		unsigned int object_valid_list_entries);

	int (*object_key_valid_set) (
		unsigned int object_handle,
		struct object_key_valid *object_key_valid_list,
		unsigned int object_key_valid_list_entries);

	int (*object_find_create) (
		unsigned int parent_object_handle,
		void *object_name,
		int object_name_len,
		unsigned int *object_find_handle);

	int (*object_find_next) (
		unsigned int object_find_handle,
		unsigned int *object_handle);

	int (*object_find_destroy) (
		unsigned int object_find_handle);

	int (*object_key_get) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void **value,
		int *value_len);

	int (*object_priv_get) (
		unsigned int jobject_handle,
		void **priv);

	int (*object_key_replace) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void *old_value,
		int old_value_len,
		void *new_value,
		int new_value_len);

	int (*object_key_delete) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		void *value,
		int value_len);

	int (*object_iter_reset) (
		unsigned int parent_object_handle);

	int (*object_iter) (
		unsigned int parent_object_handle,
		void **object_name,
		int *name_len,
		unsigned int *object_handle);

	int (*object_key_iter_reset) (
		unsigned int object_handle);

	int (*object_key_iter) (
		unsigned int parent_object_handle,
		void **key_name,
		int *key_len,
		void **value,
		int *value_len);

	int (*object_parent_get) (
		unsigned int object_handle,
		unsigned int *parent_handle);

	int (*object_name_get) (
		unsigned int object_handle,
		char *object_name,
		int *object_name_len);

	int (*object_dump) (
		unsigned int object_handle,
		FILE *file);

	int (*object_key_iter_from) (
		unsigned int parent_object_handle,
		unsigned int start_pos,
		void **key_name,
		int *key_len,
		void **value,
		int *value_len);

	int (*object_track_start) (
		unsigned int object_handle,
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

	int (*object_write_config) (char **error_string);

	int (*object_reload_config) (int flush,
				     char **error_string);

	int (*object_key_increment) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
		unsigned int *value);

	int (*object_key_decrement) (
		unsigned int object_handle,
		void *key_name,
		int key_len,
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

	/*
	 * IPC APIs
	 */
	void (*ipc_source_set) (mar_message_source_t *source, void *conn);

	int (*ipc_source_is_local) (mar_message_source_t *source);

	void *(*ipc_private_data_get) (void *conn);

	int (*ipc_response_send) (void *conn, void *msg, int mlen);

	int (*ipc_response_no_fcc) (void *conn, void *msg, int mlen);

	int (*ipc_dispatch_send) (void *conn, void *msg, int mlen);

	/*
	 * DEPRECATED
	 */
	int (*ipc_conn_send_response) (void *conn, void *msg, int mlen);

	/*
	 * DEPRECATED
	 */
	void *(*ipc_conn_partner_get) (void *conn);

	void (*ipc_fc_create) (
		void *conn,
		unsigned int service,
		char *id,
		int id_len,
		void (*flow_control_state_set_fn)
			(void *context,
				enum cs_flow_control_state flow_control_state_set),
		void *context);

	void (*ipc_fc_destroy) (
		void *conn,
		unsigned int service,
		unsigned char *id,
		int id_len);

	void (*ipc_refcnt_inc) (void *conn);

	void (*ipc_refcnt_dec) (void *conn);

	/*
	 * Totem APIs
	 */
	int (*totem_nodeid_get) (void);

	int (*totem_family_get) (void);

	int (*totem_ring_reenable) (void);

	int (*totem_mcast) (struct iovec *iovec, int iov_len, unsigned int guarantee);

	int (*totem_send_ok) (struct iovec *iovec, int iov_len);

	int (*totem_ifaces_get) (
		unsigned int nodeid,
		struct totem_ip_address *interfaces,
		char ***status,
		unsigned int *iface_count);

	char *(*totem_ifaces_print) (unsigned int nodeid);

	char *(*totem_ip_print) (struct totem_ip_address *addr);


	int (*totem_callback_token_create) (
		void **handle_out,
		enum totem_callback_token_type type,
		int delete,
		int (*callback_fn) (enum totem_callback_token_type type, void *),
		void *data);

	/*
	 * Totem open process groups API for those service engines
	 * wanting their own groups
	 */
	int (*tpg_init) (
		cs_tpg_handle *handle,

		void (*deliver_fn) (
			unsigned int nodeid,
			struct iovec *iovec,
			int iov_len,
			int endian_conversion_required),

		void (*confchg_fn) (
			enum totem_configuration_type configuration_type,
			unsigned int *member_list, int member_list_entries,
			unsigned int *left_list, int left_list_entries,
			unsigned int *joined_list, int joined_list_entries,
			struct memb_ring_id *ring_id));

	int (*tpg_exit) (
       		cs_tpg_handle handle);

	int (*tpg_join) (
		cs_tpg_handle handle,
		struct corosync_tpg_group *groups,
		int gruop_cnt);

	int (*tpg_leave) (
		cs_tpg_handle handle,
		struct corosync_tpg_group *groups,
		int gruop_cnt);

	int (*tpg_joined_mcast) (
		cs_tpg_handle handle,
		struct iovec *iovec,
		int iov_len,
		int guarantee);

	int (*tpg_joined_send_ok) (
		cs_tpg_handle handle,
		struct iovec *iovec,
		int iov_len);

	int (*tpg_groups_mcast) (
		cs_tpg_handle handle,
		int guarantee,
		struct corosync_tpg_group *groups,
		int groups_cnt,
		struct iovec *iovec,
		int iov_len);

	int (*tpg_groups_send_ok) (
		cs_tpg_handle handle,
		struct corosync_tpg_group *groups,
		int groups_cnt,
		struct iovec *iovec,
		int iov_len);

	int (*sync_request) (
		char *service_name);

	/*
	 * Plugin loading and unloading
	 */
	int (*plugin_interface_reference) (
		unsigned int *handle, 
		char *iface_name,
		int version,
		void **interface,
		void *context);

	int (*plugin_interface_release) (unsigned int handle);

	/*
	 * Service loading and unloading APIs
	*/
	unsigned int (*service_link_and_init) (
		struct corosync_api_v1 *corosync_api_v1,
		char *service_name,
		unsigned int service_ver);

	unsigned int (*service_unlink_and_exit) (
		struct corosync_api_v1 *corosync_api_v1,
		char *service_name,
		unsigned int service_ver);

	/*
	 * Error handling APIs
	 */
	void (*error_memory_failure) (void);
#define corosync_fatal_error(err) api->fatal_error ((err), __FILE__, __LINE__)
	void (*fatal_error) (cs_fatal_error_t err, const char *file, unsigned int line);
};

#define SERVICE_ID_MAKE(a,b) ( ((a)<<16) | (b) )

#define SERVICE_HANDLER_MAXIMUM_COUNT 64

struct corosync_lib_handler {
	void (*lib_handler_fn) (void *conn, void *msg);
	int response_size;
	int response_id;
	enum cs_lib_flow_control flow_control;
};

struct corosync_exec_handler {
	void (*exec_handler_fn) (void *msg, unsigned int nodeid);
	void (*exec_endian_convert_fn) (void *msg);
};

struct corosync_service_engine_iface_ver0 {
        struct corosync_service_engine *(*corosync_get_service_engine_ver0) (void);
};

struct corosync_service_engine {
	char *name;
	unsigned short id;
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
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);
	void (*sync_init) (void);
	int (*sync_process) (void);
	void (*sync_activate) (void);
	void (*sync_abort) (void);
};

#endif /* COROAPI_H_DEFINED */
	
