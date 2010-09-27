/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <sys/reboot.h>

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/coroapi.h>
#include <corosync/list.h>
#include <corosync/engine/logsys.h>
#include "../exec/fsm.h"


typedef enum {
	WD_RESOURCE_GOOD,
	WD_RESOURCE_FAILED,
	WD_RESOURCE_STATE_UNKNOWN,
	WD_RESOURCE_NOT_MONITORED
} wd_resource_state_t;

struct resource {
	hdb_handle_t handle;
	char *recovery;
	char name[CS_MAX_NAME_LENGTH];
	time_t last_updated;
	struct cs_fsm fsm;

	corosync_timer_handle_t check_timer;
	uint64_t check_timeout;
};

LOGSYS_DECLARE_SUBSYS("WD");

/*
 * Service Interfaces required by service_message_handler struct
 */
static int wd_exec_init_fn (
	struct corosync_api_v1 *corosync_api);
static int wd_exec_exit_fn (void);
static void wd_resource_check_fn (void* resource_ref);

static struct corosync_api_v1 *api;
#define WD_DEFAULT_TIMEOUT_SEC 6
#define WD_DEFAULT_TIMEOUT_MS (WD_DEFAULT_TIMEOUT_SEC * CS_TIME_MS_IN_SEC)
#define WD_MIN_TIMEOUT_MS 500
#define WD_MAX_TIMEOUT_MS (120 * CS_TIME_MS_IN_SEC)
static uint32_t watchdog_timeout = WD_DEFAULT_TIMEOUT_SEC;
static uint64_t tickle_timeout = (WD_DEFAULT_TIMEOUT_MS / 2);
static int dog = -1;
static corosync_timer_handle_t wd_timer;
static hdb_handle_t resources_obj;
static int watchdog_ok = 1;

struct corosync_service_engine wd_service_engine = {
	.name			= "corosync watchdog service",
	.id			= WD_SERVICE,
	.priority		= 1,
	.private_data_size	= 0,
	.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn		= NULL,
	.lib_exit_fn		= NULL,
	.lib_engine		= NULL,
	.lib_engine_count	= 0,
	.exec_engine		= NULL,
	.exec_engine_count	= 0,
	.confchg_fn		= NULL,
	.exec_init_fn		= wd_exec_init_fn,
	.exec_exit_fn		= wd_exec_exit_fn,
	.exec_dump_fn		= NULL,
	.sync_mode		= CS_SYNC_V2
};

static DECLARE_LIST_INIT (confchg_notify);

/*
 * F S M
 */
static void wd_config_changed (struct cs_fsm* fsm, int32_t event, void * data);
static void wd_resource_failed (struct cs_fsm* fsm, int32_t event, void * data);

enum wd_resource_state {
	WD_S_RUNNING,
	WD_S_FAILED,
	WD_S_STOPPED
};

enum wd_resource_event {
	WD_E_FAILURE,
	WD_E_CONFIG_CHANGED
};

const char * wd_running_str		= "running";
const char * wd_failed_str		= "failed";
const char * wd_failure_str		= "failure";
const char * wd_stopped_str		= "stopped";
const char * wd_config_changed_str	= "config_changed";

struct cs_fsm_entry wd_fsm_table[] = {
	{ WD_S_STOPPED,	WD_E_CONFIG_CHANGED,	wd_config_changed,	{WD_S_STOPPED, WD_S_RUNNING, -1} },
	{ WD_S_STOPPED,	WD_E_FAILURE,		NULL,			{-1} },
	{ WD_S_RUNNING,	WD_E_CONFIG_CHANGED,	wd_config_changed,	{WD_S_RUNNING, WD_S_STOPPED, -1} },
	{ WD_S_RUNNING,	WD_E_FAILURE,		wd_resource_failed,	{WD_S_FAILED, -1} },
	{ WD_S_FAILED,	WD_E_CONFIG_CHANGED,	wd_config_changed,	{WD_S_RUNNING, WD_S_STOPPED, -1} },
	{ WD_S_FAILED,	WD_E_FAILURE,		NULL,			{-1} },
};

/*
 * Dynamic loading descriptor
 */

static struct corosync_service_engine *wd_get_service_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 wd_service_engine_iface = {
	.corosync_get_service_engine_ver0	= wd_get_service_engine_ver0
};

static struct lcr_iface corosync_wd_ver0[1] = {
	{
		.name			= "corosync_wd",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL,
	}
};

static struct lcr_comp wd_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= corosync_wd_ver0
};

static struct corosync_service_engine *wd_get_service_engine_ver0 (void)
{
	return (&wd_service_engine);
}

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
	lcr_interfaces_set (&corosync_wd_ver0[0], &wd_service_engine_iface);

	lcr_component_register (&wd_comp_ver0);
}

static int object_find_or_create (
	hdb_handle_t parent_object_handle,
	hdb_handle_t *object_handle,
	const void *object_name,
	size_t object_name_len)
{
	hdb_handle_t obj_finder;
	hdb_handle_t obj;
	int ret = -1;

	api->object_find_create (
		parent_object_handle,
		object_name,
		object_name_len,
		&obj_finder);

	if (api->object_find_next (obj_finder, &obj) == 0) {
		/* found it */
		*object_handle = obj;
		ret = 0;
	}
	else {
		ret = api->object_create (parent_object_handle,
			object_handle,
			object_name, object_name_len);
	}

	api->object_find_destroy (obj_finder);
	return ret;
}

static cs_error_t str_to_uint64_t(const char* str, uint64_t *out_value, uint64_t min, uint64_t max)
{
	char *endptr;

	errno = 0;
        *out_value = strtol(str, &endptr, 0);

        /* Check for various possible errors */
	if (errno != 0 || endptr == str) {
		return CS_ERR_INVALID_PARAM;
	}

	if (*out_value > max || *out_value < min) {
		return CS_ERR_INVALID_PARAM;
	}
	return CS_OK;
}

static const char * wd_res_state_to_str(struct cs_fsm* fsm,
	int32_t state)
{
	switch (state) {
	case WD_S_STOPPED:
		return wd_stopped_str;
		break;
	case WD_S_RUNNING:
		return wd_running_str;
		break;
	case WD_S_FAILED:
		return wd_failed_str;
		break;
	}
	return NULL;
}

static const char * wd_res_event_to_str(struct cs_fsm* fsm,
	int32_t event)
{
	switch (event) {
	case WD_E_CONFIG_CHANGED:
		return wd_config_changed_str;
		break;
	case WD_E_FAILURE:
		return wd_failure_str;
		break;
	}
	return NULL;
}

/*
 * returns (CS_TRUE == OK, CS_FALSE == failed)
 */
static int32_t wd_resource_state_is_ok (struct resource *ref)
{
	hdb_handle_t resource = ref->handle;
	int res;
	char* state;
	size_t state_len;
	objdb_value_types_t type;
	uint64_t *last_updated;
	uint64_t my_time;
	uint64_t allowed_period;
	size_t last_updated_len;

	res = api->object_key_get_typed (resource,
		"last_updated", (void*)&last_updated, &last_updated_len, &type);
	if (res != 0) {
		/* key does not exist.
		*/
		return CS_FALSE;
	}
	res = api->object_key_get_typed (resource,
		"state", (void**)&state, &state_len, &type);
	if (res != 0 ||	strncmp (state, "disabled", strlen ("disabled")) == 0) {
		/* key does not exist.
		*/
		return CS_FALSE;
	}
	if (*last_updated == 0) {
		/* initial value */
		return CS_TRUE;
	}

	my_time = cs_timestamp_get();

	/*
	 * Here we check that the monitor has written a timestamp within the poll_period
	 * plus a grace factor of (0.5 * poll_period).
	 */
	allowed_period = (ref->check_timeout * MILLI_2_NANO_SECONDS * 3) / 2;
	if ((*last_updated + allowed_period) < my_time) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"last_updated %"PRIu64" ms too late, period:%"PRIu64".",
			(uint64_t)(my_time/MILLI_2_NANO_SECONDS - ((*last_updated + allowed_period) / MILLI_2_NANO_SECONDS)),
			ref->check_timeout);
		return CS_FALSE;
	}

	if (strcmp (state, wd_failed_str) == 0) {
		return CS_FALSE;
	}
	return CS_TRUE;
}

static void wd_config_changed (struct cs_fsm* fsm, int32_t event, void * data)
{
	int res;
	size_t len;
	char *state;
	objdb_value_types_t type;
	char *str;
	uint64_t tmp_value;
	uint64_t next_timeout;
	struct resource *ref = (struct resource*)data;

	next_timeout = ref->check_timeout;

	res = api->object_key_get_typed (ref->handle,
			"poll_period",
			(void**)&str, &len,
			&type);
	if (res == 0) {
		if (str_to_uint64_t(str, &tmp_value, WD_MIN_TIMEOUT_MS, WD_MAX_TIMEOUT_MS) == CS_OK) {
			log_printf (LOGSYS_LEVEL_DEBUG,
				"poll_period changing from:%"PRIu64" to %"PRIu64".",
				ref->check_timeout, tmp_value);
			/*
			 * To easy in the transition between poll_period's we are going
			 * to make the first timeout the bigger of the new and old value.
			 * This is to give the monitoring system time to adjust.
			 */
			next_timeout = CS_MAX(tmp_value, ref->check_timeout);
			ref->check_timeout = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%s ms for resource %s",
				str, ref->name);
		}
	}

	res = api->object_key_get_typed (ref->handle,
		"recovery", (void*)&ref->recovery, &len, &type);
	if (res != 0) {
		/* key does not exist.
		 */
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a recovery key.", ref->name);
		cs_fsm_state_set(&ref->fsm, WD_S_STOPPED, ref);
		return;
	}
	res = api->object_key_get_typed (ref->handle,
		"state", (void*)&state, &len, &type);
	if (res != 0) {
		/* key does not exist.
		*/
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a state key.", ref->name);
		cs_fsm_state_set(&ref->fsm, WD_S_STOPPED, ref);
		return;
	}
	if (ref->check_timer) {
		api->timer_delete(ref->check_timer);
		ref->check_timer = NULL;
	}

	if (strcmp(wd_stopped_str, state) == 0) {
		cs_fsm_state_set(&ref->fsm, WD_S_STOPPED, ref);
	} else {
		api->timer_add_duration(next_timeout * MILLI_2_NANO_SECONDS,
			ref, wd_resource_check_fn, &ref->check_timer);
		cs_fsm_state_set(&ref->fsm, WD_S_RUNNING, ref);
	}
}

static void wd_resource_failed (struct cs_fsm* fsm, int32_t event, void * data)
{
	struct resource* ref = (struct resource*)data;

	if (ref->check_timer) {
		api->timer_delete(ref->check_timer);
		ref->check_timer = NULL;
	}

	log_printf (LOGSYS_LEVEL_CRIT, "%s resource \"%s\" failed!",
		ref->recovery, (char*)ref->name);
	if (strcmp (ref->recovery, "watchdog") == 0 ||
	    strcmp (ref->recovery, "quit") == 0) {
		watchdog_ok = 0;
	}
	else if (strcmp (ref->recovery, "reboot") == 0) {
		reboot(RB_AUTOBOOT);
	}
	else if (strcmp (ref->recovery, "shutdown") == 0) {
		reboot(RB_POWER_OFF);
	}
	cs_fsm_state_set(fsm, WD_S_FAILED, data);
}

static void wd_key_changed(object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt)
{
	struct resource* ref = (struct resource*)priv_data_pt;

	if (strncmp(key_name_pt, "last_updated", key_len) == 0 ||
		strncmp(key_name_pt, "current", key_len) == 0) {
		return;
	}

	if (ref == NULL) {
		return;
	}
	cs_fsm_process(&ref->fsm, WD_E_CONFIG_CHANGED, ref);
}

static void wd_object_destroyed(
	hdb_handle_t parent_object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt)
{
	struct resource* ref = (struct resource*)priv_data_pt;

	if (ref) {
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource \"%s\" deleted from objdb!",
			ref->name);

		api->timer_delete(ref->check_timer);
		ref->check_timer = NULL;
		free(ref);
	}
}

static void wd_resource_check_fn (void* resource_ref)
{
	struct resource* ref = (struct resource*)resource_ref;

	if (wd_resource_state_is_ok (ref) == CS_FALSE) {
		cs_fsm_process(&ref->fsm, WD_E_FAILURE, ref);
		return;
	}
	api->timer_add_duration(ref->check_timeout*MILLI_2_NANO_SECONDS,
		ref, wd_resource_check_fn, &ref->check_timer);
}

/*
 * return 0   - fully configured
 * return -1  - partially configured
 */
static int32_t wd_resource_create (hdb_handle_t resource_obj)
{
	int res;
	size_t len;
	char *state;
	objdb_value_types_t type;
	char period_str[32];
	char *str;
	uint64_t tmp_value;
	struct resource *ref = malloc (sizeof (struct resource));

	ref->handle = resource_obj;
	ref->check_timeout = WD_DEFAULT_TIMEOUT_MS;
	ref->check_timer = NULL;
	api->object_name_get (resource_obj,
		ref->name,
		&len);
	ref->name[len] = '\0';
	ref->fsm.name = ref->name;
	ref->fsm.table = wd_fsm_table;
	ref->fsm.entries = sizeof(wd_fsm_table) / sizeof(struct cs_fsm_entry);
	ref->fsm.curr_entry = 0;
	ref->fsm.curr_state = WD_S_STOPPED;
	ref->fsm.state_to_str = wd_res_state_to_str;
	ref->fsm.event_to_str = wd_res_event_to_str;
	api->object_priv_set (resource_obj, NULL);

	res = api->object_key_get_typed (resource_obj,
			"poll_period",
			(void**)&str, &len,
			&type);
	if (res != 0) {
		len = snprintf (period_str, 32, "%"PRIu64"", ref->check_timeout);
		api->object_key_create_typed (resource_obj,
			"poll_period", &period_str,
			len,
			OBJDB_VALUETYPE_STRING);
	}
	else {
		if (str_to_uint64_t(str, &tmp_value, WD_MIN_TIMEOUT_MS, WD_MAX_TIMEOUT_MS) == CS_OK) {
			ref->check_timeout = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%s ms for resource %s",
				str, ref->name);
		}
	}

	api->object_track_start (resource_obj, OBJECT_TRACK_DEPTH_RECURSIVE,
			wd_key_changed, NULL, wd_object_destroyed,
			NULL, ref);

	res = api->object_key_get_typed (resource_obj,
		"recovery", (void*)&ref->recovery, &len, &type);
	if (res != 0) {
		/* key does not exist.
		 */
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a recovery key.", ref->name);
		return -1;
	}
	res = api->object_key_get_typed (resource_obj,
		"state", (void*)&state, &len, &type);
	if (res != 0) {
		/* key does not exist.
		*/
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a state key.", ref->name);
		return -1;
	}

	res = api->object_key_get_typed (resource_obj,
		"last_updated", (void*)&ref->last_updated, &len, &type);
	if (res != 0) {
		/* key does not exist.
		 */
		ref->last_updated = 0;
	}

	/*
	 * delay the first check to give the monitor time to start working.
	 */
	tmp_value = CS_MAX(ref->check_timeout * 2, WD_DEFAULT_TIMEOUT_MS);
	api->timer_add_duration(tmp_value * MILLI_2_NANO_SECONDS,
		ref,
		wd_resource_check_fn, &ref->check_timer);

	cs_fsm_state_set(&ref->fsm, WD_S_RUNNING, ref);
	return 0;
}


static void wd_tickle_fn (void* arg)
{
	ENTER();

	if (watchdog_ok) {
		if (dog > 0) {
			ioctl(dog, WDIOC_KEEPALIVE, &watchdog_ok);
		}
		api->timer_add_duration(tickle_timeout*MILLI_2_NANO_SECONDS, NULL,
			wd_tickle_fn, &wd_timer);
	}
	else {
		log_printf (LOGSYS_LEVEL_ALERT, "NOT tickling the watchdog!");
	}

}

static void wd_resource_object_created(hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt)
{
	wd_resource_create (object_handle);
}

static void wd_scan_resources (void)
{
	hdb_handle_t obj_finder;
	hdb_handle_t obj_finder2;
	hdb_handle_t resource_type;
	hdb_handle_t resource;
	int res_count = 0;

	ENTER();

	api->object_find_create (
		OBJECT_PARENT_HANDLE,
		"resources", strlen ("resources"),
		&obj_finder);

	api->object_find_next (obj_finder, &resources_obj);
	api->object_find_destroy (obj_finder);

	/* this will be the system or process level
	 */
	api->object_find_create (
		resources_obj,
		NULL, 0,
		&obj_finder);
	while (api->object_find_next (obj_finder,
			&resource_type) == 0) {

		api->object_find_create (
			resource_type,
			NULL, 0,
			&obj_finder2);

		while (api->object_find_next (obj_finder2,
				&resource) == 0) {

			if (wd_resource_create (resource) == 0) {
				res_count++;
			}
		}
		api->object_find_destroy (obj_finder2);

		api->object_track_start (resource_type, OBJECT_TRACK_DEPTH_ONE,
			NULL, wd_resource_object_created, NULL,
			NULL, NULL);
	}
	api->object_find_destroy (obj_finder);
	if (res_count == 0) {
		log_printf (LOGSYS_LEVEL_INFO, "no resources configured.");
	}
}


static void watchdog_timeout_apply (uint32_t new)
{
	struct watchdog_info ident;
	uint32_t original_timeout = watchdog_timeout;

	if (new == original_timeout) {
		return;
	}

	watchdog_timeout = new;

	if (dog > 0) {
		ioctl(dog, WDIOC_GETSUPPORT, &ident);
		if (ident.options & WDIOF_SETTIMEOUT) {
			/* yay! the dog is trained.
			 */
			ioctl(dog, WDIOC_SETTIMEOUT, &watchdog_timeout);
		}
		ioctl(dog, WDIOC_GETTIMEOUT, &watchdog_timeout);
	}

	if (watchdog_timeout == new) {
		tickle_timeout = (watchdog_timeout * CS_TIME_MS_IN_SEC)/ 2;

		/* reset the tickle timer in case it was reduced.
		 */
		api->timer_delete (wd_timer);
		api->timer_add_duration(tickle_timeout*MILLI_2_NANO_SECONDS, NULL,
			wd_tickle_fn, &wd_timer);

		log_printf (LOGSYS_LEVEL_DEBUG, "The Watchdog timeout is %d seconds\n", watchdog_timeout);
		log_printf (LOGSYS_LEVEL_DEBUG, "The tickle timeout is %"PRIu64" ms\n", tickle_timeout);
	} else {
		log_printf (LOGSYS_LEVEL_WARNING,
			"Could not change the Watchdog timeout from %d to %d seconds\n",
			original_timeout, new);
	}

}

static int setup_watchdog(void)
{
	struct watchdog_info ident;

	ENTER();
	if (access ("/dev/watchdog", W_OK) != 0) {
		log_printf (LOGSYS_LEVEL_WARNING, "No Watchdog, try modprobe <a watchdog>");
		dog = -1;
		return -1;
	}

	/* here goes, lets hope they have "Magic Close"
	 */
	dog = open("/dev/watchdog", O_WRONLY);

	if (dog == -1) {
		log_printf (LOGSYS_LEVEL_WARNING, "Watchdog exists but couldn't be opened.");
		dog = -1;
		return -1;
	}

	/* Right we have the dog.
	 * Lets see what breed it is.
	 */

	ioctl(dog, WDIOC_GETSUPPORT, &ident);
	log_printf (LOGSYS_LEVEL_INFO, "Watchdog is now been tickled by corosync.");
	log_printf (LOGSYS_LEVEL_DEBUG, "%s", ident.identity);

	watchdog_timeout_apply (watchdog_timeout);

	ioctl(dog, WDIOC_SETOPTIONS, WDIOS_ENABLECARD);

	return 0;
}

static void wd_top_level_key_changed(object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt)
{
	uint64_t tmp_value;
	int32_t tmp_value_32;

	ENTER();
	if (change_type != OBJECT_KEY_DELETED &&
		strncmp ((char*)key_name_pt, "watchdog_timeout", key_value_len) == 0) {
		if (str_to_uint64_t(key_value_pt, &tmp_value, 2, 120) == CS_OK) {
			tmp_value_32 = tmp_value;
			watchdog_timeout_apply (tmp_value_32);
		}
	}
	else {
		watchdog_timeout_apply (WD_DEFAULT_TIMEOUT_SEC);
	}
}

static void watchdog_timeout_get_initial (void)
{
	int32_t res;
	char watchdog_timeout_str[32];
	size_t watchdog_timeout_len;
	objdb_value_types_t watchdog_timeout_type;
	uint32_t tmp_value_32;
	uint64_t tmp_value;

	ENTER();

	res = api->object_key_get_typed (resources_obj,
			"watchdog_timeout",
			(void**)&watchdog_timeout_str, &watchdog_timeout_len,
			&watchdog_timeout_type);
	if (res != 0) {
		watchdog_timeout_apply (WD_DEFAULT_TIMEOUT_SEC);

		watchdog_timeout_len = snprintf (watchdog_timeout_str, 32, "%d", watchdog_timeout);
		api->object_key_create_typed (resources_obj,
			"watchdog_timeout", &watchdog_timeout_str,
			watchdog_timeout_len,
			OBJDB_VALUETYPE_STRING);
	}
	else {
		if (str_to_uint64_t(watchdog_timeout_str, &tmp_value, 2, 120) == CS_OK) {
			tmp_value_32 = tmp_value;
			watchdog_timeout_apply (tmp_value_32);
		} else {
			watchdog_timeout_apply (WD_DEFAULT_TIMEOUT_SEC);
		}
	}

	api->object_track_start (resources_obj, OBJECT_TRACK_DEPTH_ONE,
		wd_top_level_key_changed, NULL, NULL,
		NULL, NULL);

}

static int wd_exec_init_fn (
	struct corosync_api_v1 *corosync_api)
{
	hdb_handle_t obj;

	ENTER();
#ifdef COROSYNC_SOLARIS
	logsys_subsys_init();
#endif
	api = corosync_api;

	object_find_or_create (OBJECT_PARENT_HANDLE,
		&resources_obj,
		"resources", strlen ("resources"));
	object_find_or_create (resources_obj,
		&obj,
		"system", strlen ("system"));
	object_find_or_create (resources_obj,
		&obj,
		"process", strlen ("process"));

	watchdog_timeout_get_initial();

	setup_watchdog();

	wd_scan_resources();

	api->timer_add_duration(tickle_timeout*MILLI_2_NANO_SECONDS, NULL,
				wd_tickle_fn, &wd_timer);

	return 0;
}

static int wd_exec_exit_fn (void)
{
	char magic = 'V';
	ENTER();

	if (dog > 0) {
		log_printf (LOGSYS_LEVEL_INFO, "magically closing the watchdog.");
		write (dog, &magic, 1);
	}
	return 0;
}


