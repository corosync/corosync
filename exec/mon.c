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
#include <statgrab.h>

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/coroapi.h>
#include <corosync/list.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>
#include "fsm.h"

#include "service.h"

LOGSYS_DECLARE_SUBSYS ("MON");

/*
 * Service Interfaces required by service_message_handler struct
 */
static char *mon_exec_init_fn (struct corosync_api_v1 *corosync_api);

static struct corosync_api_v1 *api;
#define MON_DEFAULT_PERIOD 3000
#define MON_MIN_PERIOD 500
#define MON_MAX_PERIOD (120 * CS_TIME_MS_IN_SEC)

struct corosync_service_engine mon_service_engine = {
	.name			= "corosync resource monitoring service",
	.id			= MON_SERVICE,
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
	.exec_init_fn		= mon_exec_init_fn,
	.exec_dump_fn		= NULL
};

static DECLARE_LIST_INIT (confchg_notify);


struct resource_instance {
	const char *icmap_path;
	const char *name;
	corosync_timer_handle_t timer_handle;
	void (*update_stats_fn) (void *data);
	struct cs_fsm fsm;
	uint64_t period;
	icmap_value_types_t max_type;
	union {
		int32_t int32;
		double dbl;
	} max;
};

static void mem_update_stats_fn (void *data);
static void load_update_stats_fn (void *data);

static struct resource_instance memory_used_inst = {
	.name = "memory_used",
	.icmap_path = "resources.system.memory_used.",
	.update_stats_fn = mem_update_stats_fn,
	.max_type = ICMAP_VALUETYPE_INT32,
	.max.int32 = INT32_MAX,
	.period = MON_DEFAULT_PERIOD,
};

static struct resource_instance load_15min_inst = {
	.name = "load_15min",
	.icmap_path = "resources.system.load_15min.",
	.update_stats_fn = load_update_stats_fn,
	.max_type = ICMAP_VALUETYPE_DOUBLE,
	.max.dbl = INT32_MAX,
	.period = MON_DEFAULT_PERIOD,
};


/*
 * F S M
 */
static void mon_config_changed (struct cs_fsm* fsm, int32_t event, void * data);
static void mon_resource_failed (struct cs_fsm* fsm, int32_t event, void * data);

const char * mon_running_str = "running";
const char * mon_failed_str = "failed";
const char * mon_failure_str = "failure";
const char * mon_stopped_str = "stopped";
const char * mon_config_changed_str = "config_changed";

enum mon_resource_state {
	MON_S_STOPPED,
	MON_S_RUNNING,
	MON_S_FAILED
};
enum mon_resource_event {
	MON_E_CONFIG_CHANGED,
	MON_E_FAILURE
};

struct cs_fsm_entry mon_fsm_table[] = {
	{ MON_S_STOPPED, MON_E_CONFIG_CHANGED,	mon_config_changed,	{MON_S_STOPPED, MON_S_RUNNING, -1} },
	{ MON_S_STOPPED, MON_E_FAILURE,		NULL,			{-1} },
	{ MON_S_RUNNING, MON_E_CONFIG_CHANGED,	mon_config_changed,	{MON_S_RUNNING, MON_S_STOPPED, -1} },
	{ MON_S_RUNNING, MON_E_FAILURE,		mon_resource_failed,	{MON_S_FAILED, -1} },
	{ MON_S_FAILED,  MON_E_CONFIG_CHANGED,	mon_config_changed,	{MON_S_RUNNING, MON_S_STOPPED, -1} },
	{ MON_S_FAILED,  MON_E_FAILURE,		NULL,			{-1} },
};

struct corosync_service_engine *mon_get_service_engine_ver0 (void)
{
	return (&mon_service_engine);
}

static const char * mon_res_state_to_str(struct cs_fsm* fsm,
	int32_t state)
{
	switch (state) {
	case MON_S_STOPPED:
		return mon_stopped_str;
		break;
	case MON_S_RUNNING:
		return mon_running_str;
		break;
	case MON_S_FAILED:
		return mon_failed_str;
		break;
	}
	return NULL;
}

static const char * mon_res_event_to_str(struct cs_fsm* fsm,
	int32_t event)
{
	switch (event) {
	case MON_E_CONFIG_CHANGED:
		return mon_config_changed_str;
		break;
	case MON_E_FAILURE:
		return mon_failure_str;
		break;
	}
	return NULL;
}

static void mon_fsm_cb (struct cs_fsm *fsm, int cb_event, int32_t curr_state,
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
		log_printf (LOGSYS_LEVEL_CRIT, "Fsm: Can't find callback event!");
	        corosync_exit_error(COROSYNC_DONE_FATAL_ERR);
		break;
	}
}

static void mon_fsm_state_set (struct cs_fsm* fsm,
	enum mon_resource_state next_state, struct resource_instance* inst)
{
	enum mon_resource_state prev_state = fsm->curr_state;
	const char *state_str;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	ENTER();

	cs_fsm_state_set(fsm, next_state, inst, mon_fsm_cb);

	if (prev_state == fsm->curr_state) {
		return;
	}
	state_str = mon_res_state_to_str(fsm, fsm->curr_state);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "state");
	icmap_set_string(key_name, state_str);
}


static void mon_config_changed (struct cs_fsm* fsm, int32_t event, void * data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	char *tmp_str;
	uint64_t tmp_value;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	int run_updater;
	int scanf_res = 0;
	int32_t i32;
	double dbl;

	ENTER();

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "poll_period");
	if (icmap_get_string(key_name, &tmp_str) == CS_OK) {
		scanf_res = sscanf(tmp_str, "%"PRIu64, &tmp_value);
		if (scanf_res != 1) {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period: %s (not uint64 type) for resource %s",
				tmp_str, inst->name);
		}
		free(tmp_str);

		if (tmp_value >= MON_MIN_PERIOD && tmp_value <= MON_MAX_PERIOD) {
			log_printf (LOGSYS_LEVEL_DEBUG,
				"poll_period changing from:%"PRIu64" to %"PRIu64".",
				inst->period, tmp_value);
			inst->period = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%"PRIu64" ms for resource %s",
				tmp_value, inst->name);
		}
	}

	if (inst->timer_handle) {
		api->timer_delete(inst->timer_handle);
		inst->timer_handle = 0;
	}

	run_updater = 0;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "max");

	if (icmap_get_string(key_name, &tmp_str) == CS_OK) {
		if (inst->max_type == ICMAP_VALUETYPE_INT32) {
			if (sscanf(tmp_str, "%"PRId32, &i32) != 1) {
				inst->max.int32 = INT32_MAX;

				mon_fsm_state_set (fsm, MON_S_STOPPED, inst);
			} else {
				inst->max.int32 = i32;
				run_updater = 1;
			}
		}
		if (inst->max_type == ICMAP_VALUETYPE_DOUBLE) {
			if (sscanf(tmp_str, "%lf", &dbl) != 1) {
				inst->max.dbl = INT32_MAX;

				mon_fsm_state_set (fsm, MON_S_STOPPED, inst);
			} else {
				inst->max.dbl = dbl;
				run_updater = 1;
			}
		}
		free(tmp_str);
	}

	if (run_updater) {
		mon_fsm_state_set (fsm, MON_S_RUNNING, inst);
		/*
		 * run the updater, incase the period has shortened
		 * and to start the timer.
		 */
		inst->update_stats_fn (inst);
	}
}

void mon_resource_failed (struct cs_fsm* fsm, int32_t event, void * data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	ENTER();
	mon_fsm_state_set (fsm, MON_S_FAILED, inst);
}

static int32_t percent_mem_used_get(void)
{
	sg_mem_stats *mem_stats;
	sg_swap_stats *swap_stats;
	long long total, freemem;

#ifdef HAVE_LIBSTATGRAB_GE_090
	mem_stats = sg_get_mem_stats(NULL);
	swap_stats = sg_get_swap_stats(NULL);
#else
	mem_stats = sg_get_mem_stats();
	swap_stats = sg_get_swap_stats();
#endif

	if (mem_stats == NULL || swap_stats == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "Unable to get memory stats: %s",
			sg_str_error(sg_get_error()));
		return -1;
	}
	total = mem_stats->total + swap_stats->total;
	freemem = mem_stats->free + swap_stats->free;
	return ((total - freemem) * 100) / total;
}

static void mem_update_stats_fn (void *data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	int32_t new_value;
	uint64_t timestamp;
	char key_name[ICMAP_KEYNAME_MAXLEN];

	new_value = percent_mem_used_get();
	if (new_value > 0) {
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "current");
		icmap_set_uint32(key_name, new_value);

		timestamp = cs_timestamp_get();

		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "last_updated");
		icmap_set_uint64(key_name, timestamp);

		if (new_value > inst->max.int32 && inst->fsm.curr_state != MON_S_FAILED) {
			cs_fsm_process (&inst->fsm, MON_E_FAILURE, inst, mon_fsm_cb);
		}
	}
	api->timer_add_duration(inst->period * MILLI_2_NANO_SECONDS,
		inst, inst->update_stats_fn, &inst->timer_handle);
}

static double min15_loadavg_get(void)
{
	sg_load_stats *load_stats;

#ifdef HAVE_LIBSTATGRAB_GE_090
	load_stats = sg_get_load_stats (NULL);
#else
	load_stats = sg_get_load_stats ();
#endif
	if (load_stats == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "Unable to get load stats: %s",
			sg_str_error (sg_get_error()));
		return -1;
	}
	return load_stats->min15;
}

static void load_update_stats_fn (void *data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	uint64_t timestamp;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	double min15 = min15_loadavg_get();

	if (min15 > 0) {
		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "current");
		icmap_set_double(key_name, min15);

		timestamp = cs_timestamp_get();

		snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "last_updated");
		icmap_set_uint64(key_name, timestamp);

		if (min15 > inst->max.dbl && inst->fsm.curr_state != MON_S_FAILED) {
			cs_fsm_process (&inst->fsm, MON_E_FAILURE, inst, mon_fsm_cb);
		}
	}

	api->timer_add_duration(inst->period * MILLI_2_NANO_SECONDS,
		inst, inst->update_stats_fn, &inst->timer_handle);
}

static void mon_key_changed_cb (
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_value,
	struct icmap_notify_value old_value,
	void *user_data)
{
	struct resource_instance* inst = (struct resource_instance*)user_data;
	char *last_key_part;

	if (event == ICMAP_TRACK_DELETE && inst) {
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource \"%s\" deleted from cmap!",
			inst->name);

		cs_fsm_process (&inst->fsm, MON_E_CONFIG_CHANGED, inst, mon_fsm_cb);
	}

	if (event == ICMAP_TRACK_MODIFY) {
		last_key_part = strrchr(key_name, '.');
		if (last_key_part == NULL)
			return ;

		last_key_part++;
		if (strcmp(last_key_part, "max") == 0 ||
		    strcmp(last_key_part, "poll_period") == 0) {
			ENTER();
			cs_fsm_process (&inst->fsm, MON_E_CONFIG_CHANGED, inst, mon_fsm_cb);
		}
	}
}

static void mon_instance_init (struct resource_instance* inst)
{
	uint64_t tmp_value;
	char key_name[ICMAP_KEYNAME_MAXLEN];
	icmap_track_t icmap_track = NULL;
	char *tmp_str;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "current");
	if (inst->max_type == ICMAP_VALUETYPE_INT32) {
		icmap_set_int32(key_name, 0);
	} else {
		icmap_set_double(key_name, 0);
	}

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "last_updated");
	icmap_set_uint64(key_name, 0);

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "state");
	icmap_set_string(key_name, mon_stopped_str);

	inst->fsm.name = inst->name;
	inst->fsm.curr_entry = 0;
	inst->fsm.curr_state = MON_S_STOPPED;
	inst->fsm.table = mon_fsm_table;
	inst->fsm.entries = sizeof(mon_fsm_table) / sizeof(struct cs_fsm_entry);
	inst->fsm.state_to_str = mon_res_state_to_str;
	inst->fsm.event_to_str = mon_res_event_to_str;

	snprintf(key_name, ICMAP_KEYNAME_MAXLEN, "%s%s", inst->icmap_path, "poll_period");
	if (icmap_get_string(key_name, &tmp_str) != CS_OK ||
	    sscanf(tmp_str, "%"PRIu64, &tmp_value) != 1) {
		icmap_set_uint64(key_name, inst->period);
	}
	else {
		if (tmp_value >= MON_MIN_PERIOD && tmp_value <= MON_MAX_PERIOD) {
			inst->period = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%"PRIu64" ms for resource %s",
				tmp_value, inst->name);
		}
		free(tmp_str);
	}
	cs_fsm_process (&inst->fsm, MON_E_CONFIG_CHANGED, inst, mon_fsm_cb);

	icmap_track_add(inst->icmap_path,
			ICMAP_TRACK_ADD | ICMAP_TRACK_MODIFY | ICMAP_TRACK_DELETE | ICMAP_TRACK_PREFIX,
			mon_key_changed_cb, inst, &icmap_track);
}

static char *mon_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
#ifdef HAVE_LIBSTATGRAB_GE_090
	sg_init(1);
#else
	sg_init();
#endif

	api = corosync_api;

	mon_instance_init (&memory_used_inst);
	mon_instance_init (&load_15min_inst);

	return NULL;
}


