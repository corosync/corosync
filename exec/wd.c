/*
 * Copyright (c) 2010-2012 Red Hat, Inc.
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
#include <corosync/coroapi.h>
#include <corosync/list.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>
#include "fsm.h"

#include "service.h"

typedef enum {
	WD_RESOURCE_GOOD,
	WD_RESOURCE_FAILED,
	WD_RESOURCE_STATE_UNKNOWN,
	WD_RESOURCE_NOT_MONITORED
} wd_resource_state_t;

struct resource {
	char res_path[ICMAP_KEYNAME_MAXLEN];
	char *recovery;
	char name[CS_MAX_NAME_LENGTH];
	time_t last_updated;
	struct cs_fsm fsm;

	corosync_timer_handle_t check_timer;
	uint64_t check_timeout;
	icmap_track_t icmap_track;
};

LOGSYS_DECLARE_SUBSYS("WD");

/*
 * Service Interfaces required by service_message_handler struct
 */
static char *wd_exec_init_fn (struct corosync_api_v1 *corosync_api);
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
static int watchdog_ok = 1;
static char *watchdog_device = "/dev/watchdog";

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
	.exec_dump_fn		= NULL
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

struct corosync_service_engine *wd_get_service_engine_ver0 (void)
{
	return (&wd_service_engine);
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

static void wd_fsm_cb (struct cs_fsm *fsm, int cb_event, int32_t curr_state,
	int32_t next_state, int32_t fsm_event, void *data)
{
	switch (cb_event) {
	case CS_FSM_CB_EVENT_PROCESS_NF:
		log_printf (LOGSYS_LEVEL_ERROR, "Fsm:%s could not find event \"%s\" in state \"%s\"",
			fsm->name, fsm->event_to_str(fsm, fsm_event), fsm->state_to_str(fsm, curr_state));
		corosync_exit_error(COROSYNC_DONE_FATAL_ERR);
		break;
	case CS_FSM_CB_EVENT_STATE_SET:
		log_printf (LOGSYS_LEVEL_INFO, "Fsm:%s event \"%s\", state \"%s\" --> \"%s\"",
			fsm->name,
			fsm->event_to_str(fsm, fsm_event),
			fsm->state_to_str(fsm, fsm->table[fsm->curr_entry].curr_state),
			fsm->state_to_str(fsm, next_state));
		break;
	case CS_FSM_CB_EVENT_STATE_SET_NF:
		log_printf (LOGSYS_LEVEL_CRIT, "Fsm:%s Can't change state from \"%s\" to \"%s\" (event was \"%s\")",
			fsm->name,
			fsm->state_to_str(fsm, fsm->table[fsm->curr_entry].curr_state),
			fsm->state_to_str(fsm, next_state),
			fsm->event_to_str(fsm, fsm_event));
	        corosync_exit_error(COROSYNC_DONE_FATAL_ERR);
		break;
	default:
		log_printf (LOGSYS_LEVEL_CRIT, "Fsm: Unknown callback event!");
	        corosync_exit_error(COROSYNC_DONE_FATAL_ERR);
		break;
	}
}

/*
 * returns (CS_TRUE == OK, CS_FALSE == failed)
 */
static int32_t wd_resource_state_is_ok (struct resource *ref)
{
	char* state = NULL;
	uint64_t last_updated;
	uint64_t my_time;
	uint64_t allowed_period;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", ref->res_path, "last_updated");
	if (icmap_get_uint64(key_name, &last_updated) != CS_OK) {
		/* key does not exist.
		*/
		return CS_FALSE;
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", ref->res_path, "state");
	if (icmap_get_string(key_name, &state) != CS_OK || strcmp(state, "disabled") == 0) {
		/* key does not exist.
		*/
		if (state != NULL)
			free(state);

		return CS_FALSE;
	}

	if (last_updated == 0) {
		/* initial value */
		free(state);
		return CS_TRUE;
	}

	my_time = cs_timestamp_get();

	/*
	 * Here we check that the monitor has written a timestamp within the poll_period
	 * plus a grace factor of (0.5 * poll_period).
	 */
	allowed_period = (ref->check_timeout * MILLI_2_NANO_SECONDS * 3) / 2;
	if ((last_updated + allowed_period) < my_time) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"last_updated %"PRIu64" ms too late, period:%"PRIu64".",
			(uint64_t)(my_time/MILLI_2_NANO_SECONDS - ((last_updated + allowed_period) / MILLI_2_NANO_SECONDS)),
			ref->check_timeout);
		free(state);
		return CS_FALSE;
	}

	if (strcmp (state, wd_failed_str) == 0) {
		free(state);
		return CS_FALSE;
	}

	free(state);
	return CS_TRUE;
}

static void wd_config_changed (struct cs_fsm* fsm, int32_t event, void * data)
{
	char *state;
	uint64_t tmp_value;
	uint64_t next_timeout;
	struct resource *ref = (struct resource*)data;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	next_timeout = ref->check_timeout;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", ref->res_path, "poll_period");
	if (icmap_get_uint64(ref->res_path, &tmp_value) == CS_OK) {
		if (tmp_value >= WD_MIN_TIMEOUT_MS && tmp_value <= WD_MAX_TIMEOUT_MS) {
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
				"Could NOT use poll_period:%"PRIu64" ms for resource %s",
				tmp_value, ref->name);
		}
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", ref->res_path, "recovery");
	if (icmap_get_string(key_name, &ref->recovery) != CS_OK) {
		/* key does not exist.
		 */
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a recovery key.", ref->name);
		cs_fsm_state_set(&ref->fsm, WD_S_STOPPED, ref, wd_fsm_cb);
		return;
	}
	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", ref->res_path, "state");
	if (icmap_get_string(key_name, &state) != CS_OK) {
		/* key does not exist.
		*/
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a state key.", ref->name);
		cs_fsm_state_set(&ref->fsm, WD_S_STOPPED, ref, wd_fsm_cb);
		return;
	}
	if (ref->check_timer) {
		api->timer_delete(ref->check_timer);
		ref->check_timer = 0;
	}

	if (strcmp(wd_stopped_str, state) == 0) {
		cs_fsm_state_set(&ref->fsm, WD_S_STOPPED, ref, wd_fsm_cb);
	} else {
		api->timer_add_duration(next_timeout * MILLI_2_NANO_SECONDS,
			ref, wd_resource_check_fn, &ref->check_timer);
		cs_fsm_state_set(&ref->fsm, WD_S_RUNNING, ref, wd_fsm_cb);
	}
	free(state);
}

static void wd_resource_failed (struct cs_fsm* fsm, int32_t event, void * data)
{
	struct resource* ref = (struct resource*)data;

	if (ref->check_timer) {
		api->timer_delete(ref->check_timer);
		ref->check_timer = 0;
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
	cs_fsm_state_set(fsm, WD_S_FAILED, data, wd_fsm_cb);
}

static void wd_key_changed(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	struct resource* ref = (struct resource*)user_data;
	char *last_key_part;

	if (ref == NULL) {
		return ;
	}

	last_key_part = strrchr(key_name, '.');
	if (last_key_part == NULL) {
		return ;
	}
	last_key_part++;

	if (event == ICMAP_TRACK_ADD || event == ICMAP_TRACK_MODIFY) {
		if (strcmp(last_key_part, "last_updated") == 0 ||
			strcmp(last_key_part, "current") == 0) {
			return;
		}

		cs_fsm_process(&ref->fsm, WD_E_CONFIG_CHANGED, ref, wd_fsm_cb);
	}

	if (event == ICMAP_TRACK_DELETE && ref != NULL) {
		if (strcmp(last_key_part, "state") != 0) {
			return ;
		}

		log_printf (LOGSYS_LEVEL_WARNING,
			"resource \"%s\" deleted from cmap!",
			ref->name);

		api->timer_delete(ref->check_timer);
		ref->check_timer = 0;
		icmap_track_delete(ref->icmap_track);

		free(ref);
	}
}

static void wd_resource_check_fn (void* resource_ref)
{
	struct resource* ref = (struct resource*)resource_ref;

	if (wd_resource_state_is_ok (ref) == CS_FALSE) {
		cs_fsm_process(&ref->fsm, WD_E_FAILURE, ref, wd_fsm_cb);
		return;
	}
	api->timer_add_duration(ref->check_timeout*MILLI_2_NANO_SECONDS,
		ref, wd_resource_check_fn, &ref->check_timer);
}

/*
 * return 0   - fully configured
 * return -1  - partially configured
 */
static int32_t wd_resource_create (char *res_path, char *res_name)
{
	char *state;
	uint64_t tmp_value;
	struct resource *ref = calloc (1, sizeof (struct resource));
	char key_name[ICMAP_KEYNAME_MAXLEN];

	strcpy(ref->res_path, res_path);
	ref->check_timeout = WD_DEFAULT_TIMEOUT_MS;
	ref->check_timer = 0;

	strcpy(ref->name, res_name);
	ref->fsm.name = ref->name;
	ref->fsm.table = wd_fsm_table;
	ref->fsm.entries = sizeof(wd_fsm_table) / sizeof(struct cs_fsm_entry);
	ref->fsm.curr_entry = 0;
	ref->fsm.curr_state = WD_S_STOPPED;
	ref->fsm.state_to_str = wd_res_state_to_str;
	ref->fsm.event_to_str = wd_res_event_to_str;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", res_path, "poll_period");
	if (icmap_get_uint64(key_name, &tmp_value) != CS_OK) {
		icmap_set_uint64(key_name, ref->check_timeout);
	} else {
		if (tmp_value >= WD_MIN_TIMEOUT_MS && tmp_value <= WD_MAX_TIMEOUT_MS) {
			ref->check_timeout = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%"PRIu64" ms for resource %s",
				tmp_value, ref->name);
		}
	}

	icmap_track_add(res_path,
			ICMAP_TRACK_ADD | ICMAP_TRACK_MODIFY | ICMAP_TRACK_DELETE | ICMAP_TRACK_PREFIX,
			wd_key_changed,
			ref, &ref->icmap_track);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", res_path, "recovery");
	if (icmap_get_string(key_name, &ref->recovery) != CS_OK) {
		/* key does not exist.
		 */
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a recovery key.", ref->name);
		return -1;
	}
	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", res_path, "state");
	if (icmap_get_string(key_name, &state) != CS_OK) {
		/* key does not exist.
		*/
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource %s missing a state key.", ref->name);
		return -1;
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", res_path, "last_updated");
	if (icmap_get_uint64(key_name, &tmp_value) != CS_OK) {
		/* key does not exist.
		 */
		ref->last_updated = 0;
	} else {
		ref->last_updated = tmp_value;
	}

	/*
	 * delay the first check to give the monitor time to start working.
	 */
	tmp_value = CS_MAX(ref->check_timeout * 2, WD_DEFAULT_TIMEOUT_MS);
	api->timer_add_duration(tmp_value * MILLI_2_NANO_SECONDS,
		ref,
		wd_resource_check_fn, &ref->check_timer);

	cs_fsm_state_set(&ref->fsm, WD_S_RUNNING, ref, wd_fsm_cb);
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

static void wd_resource_created_cb(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	char res_name[ICMAP_KEYNAME_MAXLEN];
	char res_type[ICMAP_KEYNAME_MAXLEN];
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	int res;

	if (event != ICMAP_TRACK_ADD) {
		return ;
	}

	res = sscanf(key_name, "resources.%[^.].%[^.].%[^.]", res_type, res_name, tmp_key);
	if (res != 3) {
		return ;
	}

	if (strcmp(tmp_key, "state") != 0) {
		return ;
	}

	snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "resources.%s.%s.", res_type, res_name);
	wd_resource_create (tmp_key, res_name);
}

static void wd_scan_resources (void)
{
	int res_count = 0;
	icmap_track_t icmap_track = NULL;
	icmap_iter_t iter;
	const char *key_name;
	int res;
	char res_name[ICMAP_KEYNAME_MAXLEN];
	char res_type[ICMAP_KEYNAME_MAXLEN];
	char tmp_key[ICMAP_KEYNAME_MAXLEN];

	ENTER();

	iter = icmap_iter_init("resources.");
	while ((key_name = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(key_name, "resources.%[^.].%[^.].%[^.]", res_type, res_name, tmp_key);
		if (res != 3) {
			continue ;
		}

		if (strcmp(tmp_key, "state") != 0) {
			continue ;
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "resources.%s.%s.", res_type, res_name);
		if (wd_resource_create (tmp_key, res_name) == 0) {
			res_count++;
		}
	}
	icmap_iter_finalize(iter);

	icmap_track_add("resources.process.", ICMAP_TRACK_ADD | ICMAP_TRACK_PREFIX,
			wd_resource_created_cb, NULL, &icmap_track);
	icmap_track_add("resources.system.", ICMAP_TRACK_ADD | ICMAP_TRACK_PREFIX,
			wd_resource_created_cb, NULL, &icmap_track);

	if (res_count == 0) {
		log_printf (LOGSYS_LEVEL_INFO, "no resources configured.");
	}
}


static void watchdog_timeout_apply (uint32_t new)
{
	struct watchdog_info ident;
	uint32_t original_timeout = 0;

	if (dog > 0) {
		ioctl(dog, WDIOC_GETTIMEOUT, &original_timeout);
	}

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

		log_printf (LOGSYS_LEVEL_DEBUG, "The Watchdog timeout is %d seconds", watchdog_timeout);
		log_printf (LOGSYS_LEVEL_DEBUG, "The tickle timeout is %"PRIu64" ms", tickle_timeout);
	} else {
		log_printf (LOGSYS_LEVEL_WARNING,
			"Could not change the Watchdog timeout from %d to %d seconds",
			original_timeout, new);
	}

}

static int setup_watchdog(void)
{
	struct watchdog_info ident;
	char *str;

	ENTER();

	if (icmap_get_string("resources.watchdog_device", &str) == CS_OK) {
		if (strcmp (str, "off") == 0) {
			log_printf (LOGSYS_LEVEL_WARNING, "Watchdog disabled by configuration");
			free(str);
			dog = -1;
			return -1;
		} else {
			watchdog_device = str;
		}
	}

	if (access (watchdog_device, W_OK) != 0) {
		log_printf (LOGSYS_LEVEL_WARNING, "No watchdog %s, try modprobe <a watchdog>", watchdog_device);
		dog = -1;
		return -1;
	}

	/* here goes, lets hope they have "Magic Close"
	 */
	dog = open(watchdog_device, O_WRONLY);

	if (dog == -1) {
		log_printf (LOGSYS_LEVEL_WARNING, "Watchdog %s exists but couldn't be opened.", watchdog_device);
		dog = -1;
		return -1;
	}

	/* Right we have the dog.
	 * Lets see what breed it is.
	 */

	ioctl(dog, WDIOC_GETSUPPORT, &ident);
	log_printf (LOGSYS_LEVEL_INFO, "Watchdog %s is now being tickled by corosync.", watchdog_device);
	log_printf (LOGSYS_LEVEL_DEBUG, "%s", ident.identity);

	watchdog_timeout_apply (watchdog_timeout);

	ioctl(dog, WDIOC_SETOPTIONS, WDIOS_ENABLECARD);

	return 0;
}

static void wd_top_level_key_changed(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	uint32_t tmp_value_32;

	ENTER();

	if (icmap_get_uint32("resources.watchdog_timeout", &tmp_value_32) == CS_OK) {
		if (tmp_value_32 >= 2 && tmp_value_32 <= 120) {
			watchdog_timeout_apply (tmp_value_32);
			return;
		}
	}

	log_printf (LOGSYS_LEVEL_WARNING,
		"Set watchdog_timeout is out of range (2..120).");
	icmap_set_uint32("resources.watchdog_timeout", watchdog_timeout);
}

static void watchdog_timeout_get_initial (void)
{
	uint32_t tmp_value_32;
	icmap_track_t icmap_track = NULL;

	ENTER();

	if (icmap_get_uint32("resources.watchdog_timeout", &tmp_value_32) != CS_OK) {
		watchdog_timeout_apply (WD_DEFAULT_TIMEOUT_SEC);

		icmap_set_uint32("resources.watchdog_timeout", watchdog_timeout);
	}
	else {
		if (tmp_value_32 >= 2 && tmp_value_32 <= 120) {
			watchdog_timeout_apply (tmp_value_32);
		}
		else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Set watchdog_timeout is out of range (2..120).");
			log_printf (LOGSYS_LEVEL_INFO,
				"use default value %d seconds.", WD_DEFAULT_TIMEOUT_SEC);
			watchdog_timeout_apply (WD_DEFAULT_TIMEOUT_SEC);
			icmap_set_uint32("resources.watchdog_timeout", watchdog_timeout);
		}
	}

	icmap_track_add("resources.watchdog_timeout", ICMAP_TRACK_MODIFY,
			wd_top_level_key_changed, NULL, &icmap_track);

}

static char *wd_exec_init_fn (struct corosync_api_v1 *corosync_api)
{

	ENTER();

	api = corosync_api;

	watchdog_timeout_get_initial();

	setup_watchdog();

	wd_scan_resources();

	return NULL;
}

static int wd_exec_exit_fn (void)
{
	char magic = 'V';
	ENTER();

	if (dog > 0) {
		log_printf (LOGSYS_LEVEL_INFO, "magically closing the watchdog.");
		if (write (dog, &magic, 1) == -1) {
		    log_printf (LOGSYS_LEVEL_ERROR, "failed to write %c to dog(%d).", magic, dog);
		}
	}
	return 0;
}


