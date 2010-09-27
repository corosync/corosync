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
#if defined(HAVE_LIBSTATGRAB)
#include <statgrab.h>
#endif

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/coroapi.h>
#include <corosync/list.h>
#include <corosync/engine/logsys.h>
#include "../exec/fsm.h"


LOGSYS_DECLARE_SUBSYS ("MON");

/*
 * Service Interfaces required by service_message_handler struct
 */
static int mon_exec_init_fn (
	struct corosync_api_v1 *corosync_api);

static struct corosync_api_v1 *api;
static hdb_handle_t resources_obj;
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
	.exec_dump_fn		= NULL,
	.sync_mode		= CS_SYNC_V2
};

static DECLARE_LIST_INIT (confchg_notify);


struct resource_instance {
	hdb_handle_t handle;
	const char *name;
	corosync_timer_handle_t timer_handle;
	void (*update_stats_fn) (void *data);
	struct cs_fsm fsm;
	uint64_t period;
	objdb_value_types_t max_type;
	union {
		int32_t int32;
		double dbl;
	} max;
};

static void mem_update_stats_fn (void *data);
static void load_update_stats_fn (void *data);

static struct resource_instance memory_used_inst = {
	.name = "memory_used",
	.update_stats_fn = mem_update_stats_fn,
	.max_type = OBJDB_VALUETYPE_INT32,
	.max.int32 = INT32_MAX,
	.period = MON_DEFAULT_PERIOD,
};

static struct resource_instance load_15min_inst = {
	.name = "load_15min",
	.update_stats_fn = load_update_stats_fn,
	.max_type = OBJDB_VALUETYPE_DOUBLE,
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

/*
 * Dynamic loading descriptor
 */

static struct corosync_service_engine *mon_get_service_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 mon_service_engine_iface = {
	.corosync_get_service_engine_ver0	= mon_get_service_engine_ver0
};

static struct lcr_iface corosync_mon_ver0[1] = {
	{
		.name			= "corosync_mon",
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

static struct lcr_comp mon_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= corosync_mon_ver0
};

static struct corosync_service_engine *mon_get_service_engine_ver0 (void)
{
	return (&mon_service_engine);
}

#ifdef COROSYNC_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void) {
#endif
	lcr_interfaces_set (&corosync_mon_ver0[0], &mon_service_engine_iface);

	lcr_component_register (&mon_comp_ver0);
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

static void mon_fsm_state_set (struct cs_fsm* fsm,
	enum mon_resource_state next_state, struct resource_instance* inst)
{
	enum mon_resource_state prev_state = fsm->curr_state;
	const char *state_str;

	ENTER();

	cs_fsm_state_set(fsm, next_state, inst);

	if (prev_state == fsm->curr_state) {
		return;
	}
	state_str = mon_res_state_to_str(fsm, fsm->curr_state);

	api->object_key_replace (inst->handle,
		"state", strlen ("state"),
		state_str, strlen (state_str));
}


static void mon_config_changed (struct cs_fsm* fsm, int32_t event, void * data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	char *str;
	size_t str_len;
	objdb_value_types_t type;
	uint64_t tmp_value;
	int32_t res;

	ENTER();

	res = api->object_key_get_typed (inst->handle,
			"poll_period",
			(void**)&str, &str_len,
			&type);
	if (res == 0) {
		if (str_to_uint64_t(str, &tmp_value, MON_MIN_PERIOD, MON_MAX_PERIOD) == CS_OK) {
			log_printf (LOGSYS_LEVEL_DEBUG,
				"poll_period changing from:%"PRIu64" to %"PRIu64".",
				inst->period, tmp_value);
			inst->period = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%s ms for resource %s",
				str, inst->name);
		}
	}

	if (inst->timer_handle) {
		api->timer_delete(inst->timer_handle);
		inst->timer_handle = 0;
	}
	res = api->object_key_get_typed (inst->handle, "max",
			(void**)&str, &str_len, &type);
	if (res != 0) {
		if (inst->max_type == OBJDB_VALUETYPE_INT32) {
			inst->max.int32 = INT32_MAX;
		} else
		if (inst->max_type == OBJDB_VALUETYPE_DOUBLE) {
			inst->max.dbl = INT32_MAX;
		}
		mon_fsm_state_set (fsm, MON_S_STOPPED, inst);
	} else {
		if (inst->max_type == OBJDB_VALUETYPE_INT32) {
			inst->max.int32 = strtol (str, NULL, 0);
		} else
		if (inst->max_type == OBJDB_VALUETYPE_DOUBLE) {
			inst->max.dbl = strtod (str, NULL);
		}
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
#if defined(HAVE_LIBSTATGRAB)
	sg_mem_stats *mem_stats;
	sg_swap_stats *swap_stats;
	long long total, freemem;

	mem_stats = sg_get_mem_stats();
	swap_stats = sg_get_swap_stats();

	if (mem_stats == NULL || swap_stats != NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "Unable to get memory stats: %s\n",
			sg_str_error(sg_get_error()));
		return -1;
	}
	total = mem_stats->total + swap_stats->total;
	freemem = mem_stats->free + swap_stats->free;
	return ((total - freemem) * 100) / total;
#else
#if defined(COROSYNC_LINUX)
	char *line_ptr;
	char line[512];
	unsigned long long value;
	FILE *f;
	long long total = 0;
	long long freemem = 0;

	if ((f = fopen("/proc/meminfo", "r")) == NULL) {
		return -1;
	}

	while ((line_ptr = fgets(line, sizeof(line), f)) != NULL) {
		if (sscanf(line_ptr, "%*s %llu kB", &value) != 1) {
			continue;
		}
		value *= 1024;

		if (strncmp(line_ptr, "MemTotal:", 9) == 0) {
			total += value;
		} else if (strncmp(line_ptr, "MemFree:", 8) == 0) {
			freemem += value;
		} else if (strncmp(line_ptr, "SwapTotal:", 10) == 0) {
			total += value;
		} else if (strncmp(line_ptr, "SwapFree:", 9) == 0) {
			freemem += value;
		}
	}

	fclose(f);
	return ((total - freemem) * 100) / total;
#else
#error need libstatgrab or linux.
#endif /* COROSYNC_LINUX */
#endif /* HAVE_LIBSTATGRAB */
}


static void mem_update_stats_fn (void *data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	int32_t new_value;
	uint64_t timestamp;

	new_value = percent_mem_used_get();
	if (new_value > 0) {
		api->object_key_replace (inst->handle,
			"current", strlen("current"),
			&new_value, sizeof(new_value));

		timestamp = cs_timestamp_get();

		api->object_key_replace (inst->handle,
			"last_updated", strlen("last_updated"),
			&timestamp, sizeof(uint64_t));

		if (new_value > inst->max.int32 && inst->fsm.curr_state != MON_S_FAILED) {
			cs_fsm_process (&inst->fsm, MON_E_FAILURE, inst);
		}
	}
	api->timer_add_duration(inst->period * MILLI_2_NANO_SECONDS,
		inst, inst->update_stats_fn, &inst->timer_handle);
}

static double min15_loadavg_get(void)
{
#if defined(HAVE_LIBSTATGRAB)
	sg_load_stats *load_stats;
	load_stats = sg_get_load_stats ();
	if (load_stats == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "Unable to get load stats: %s\n",
			sg_str_error (sg_get_error()));
		return -1;
	}
	return load_stats->min15;
#else
#if defined(COROSYNC_LINUX)
	double loadav[3];
	if (getloadavg(loadav,3) < 0) {
		return -1;
	}
	return loadav[2];
#else
#error need libstatgrab or linux.
#endif /* COROSYNC_LINUX */
#endif /* HAVE_LIBSTATGRAB */
}

static void load_update_stats_fn (void *data)
{
	struct resource_instance * inst = (struct resource_instance *)data;
	uint64_t timestamp;
	int32_t res = 0;
	double min15 = min15_loadavg_get();

	if (min15 > 0) {
		res = api->object_key_replace (inst->handle,
			"current", strlen("current"),
			&min15, sizeof (min15));
		if (res != 0) {
			log_printf (LOGSYS_LEVEL_ERROR, "replace current failed: %d", res);
		}
		timestamp = cs_timestamp_get();

		res = api->object_key_replace (inst->handle,
			"last_updated", strlen("last_updated"),
			&timestamp, sizeof(uint64_t));
		if (res != 0) {
			log_printf (LOGSYS_LEVEL_ERROR, "replace last_updated failed: %d", res);
		}
		if (min15 > inst->max.dbl && inst->fsm.curr_state != MON_S_FAILED) {
			cs_fsm_process (&inst->fsm, MON_E_FAILURE, &inst);
		}
	}

	api->timer_add_duration(inst->period * MILLI_2_NANO_SECONDS,
		inst, inst->update_stats_fn, &inst->timer_handle);
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

static void mon_object_destroyed(
	hdb_handle_t parent_object_handle,
	const void *name_pt, size_t name_len,
	void *priv_data_pt)
{
	struct resource_instance* inst = (struct resource_instance*)priv_data_pt;

	if (inst) {
		log_printf (LOGSYS_LEVEL_WARNING,
			"resource \"%s\" deleted from objdb!",
			inst->name);

		cs_fsm_process (&inst->fsm, MON_E_CONFIG_CHANGED, inst);
	}
}


static void mon_key_change_notify (object_change_type_t change_type,
	hdb_handle_t parent_object_handle,
	hdb_handle_t object_handle,
	const void *object_name_pt, size_t object_name_len,
	const void *key_name_pt, size_t key_len,
	const void *key_value_pt, size_t key_value_len,
	void *priv_data_pt)
{
	struct resource_instance* inst = (struct resource_instance*)priv_data_pt;

	if ((strncmp ((char*)key_name_pt, "max", key_len) == 0) ||
		(strncmp ((char*)key_name_pt, "poll_period", key_len) == 0)) {
		ENTER();
		cs_fsm_process (&inst->fsm, MON_E_CONFIG_CHANGED, inst);
	}
}

static void mon_instance_init (hdb_handle_t parent, struct resource_instance* inst)
{
	int32_t res;
	char mon_period_str[32];
	char *str;
	size_t mon_period_len;
	objdb_value_types_t mon_period_type;
	uint64_t tmp_value;
	int32_t zero_32 = 0;
	time_t zero_64 = 0;
	double zero_double = 0;

	object_find_or_create (parent,
		&inst->handle,
		inst->name, strlen (inst->name));

	if (inst->max_type == OBJDB_VALUETYPE_INT32) {
		api->object_key_create_typed (inst->handle,
			"current", &zero_32,
			sizeof (zero_32), inst->max_type);
	} else {
		api->object_key_create_typed (inst->handle,
			"current", &zero_double,
			sizeof (zero_double), inst->max_type);
	}

	api->object_key_create_typed (inst->handle,
		"last_updated", &zero_64,
		sizeof (uint64_t), OBJDB_VALUETYPE_UINT64);

	api->object_key_create_typed (inst->handle,
		"state", mon_stopped_str, strlen (mon_stopped_str),
		OBJDB_VALUETYPE_STRING);

	inst->fsm.name = inst->name;
	inst->fsm.curr_entry = 0;
	inst->fsm.curr_state = MON_S_STOPPED;
	inst->fsm.table = mon_fsm_table;
	inst->fsm.entries = sizeof(mon_fsm_table) / sizeof(struct cs_fsm_entry);
	inst->fsm.state_to_str = mon_res_state_to_str;
	inst->fsm.event_to_str = mon_res_event_to_str;

	res = api->object_key_get_typed (inst->handle,
			"poll_period",
			(void**)&str, &mon_period_len,
			&mon_period_type);
	if (res != 0) {
		mon_period_len = snprintf (mon_period_str, 32, "%"PRIu64"",
			inst->period);
		api->object_key_create_typed (inst->handle,
			"poll_period", &mon_period_str,
			mon_period_len,
			OBJDB_VALUETYPE_STRING);
	}
	else {
		if (str_to_uint64_t(str, &tmp_value, MON_MIN_PERIOD, MON_MAX_PERIOD) == CS_OK) {
			inst->period = tmp_value;
		} else {
			log_printf (LOGSYS_LEVEL_WARNING,
				"Could NOT use poll_period:%s ms for resource %s",
				str, inst->name);
		}
	}
	cs_fsm_process (&inst->fsm, MON_E_CONFIG_CHANGED, inst);

	api->object_track_start (inst->handle, OBJECT_TRACK_DEPTH_RECURSIVE,
		mon_key_change_notify,
		NULL, mon_object_destroyed, NULL, inst);

}

static int mon_exec_init_fn (
	struct corosync_api_v1 *corosync_api)
{
	hdb_handle_t obj;
	hdb_handle_t parent;

#ifdef HAVE_LIBSTATGRAB
	sg_init();
#endif /* HAVE_LIBSTATGRAB */

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

	parent = obj;

	mon_instance_init (parent, &memory_used_inst);
	mon_instance_init (parent, &load_15min_inst);

	return 0;
}


