#include <config.h>

#include <sys/types.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <corosync/corotypes.h>
#include <corosync/quorum.h>

static quorum_handle_t g_handle;

static void quorum_notification_fn(
	quorum_handle_t handle,
	uint32_t quorate,
	struct quorum_ring_id ring_id,
	uint32_t view_list_entries,
	const uint32_t *view_list)
{
	int i;

	printf("quorum notification called \n");
	printf("  quorate   = %lu\n", (long unsigned int) quorate);
	printf("  ring id   = " CS_PRI_RING_ID "\n", ring_id.nodeid, ring_id.seq);
	printf("  num nodes = %lu ", (long unsigned int) view_list_entries);

	for (i=0; i<view_list_entries; i++) {
		printf(" " CS_PRI_NODE_ID, view_list[i]);
	}
	printf("\n");
}

static void nodelist_notification_fn(
	quorum_handle_t handle,
	struct quorum_ring_id ring_id,
	uint32_t member_list_entries, const uint32_t *member_list,
	uint32_t joined_list_entries, const uint32_t *joined_list,
	uint32_t left_list_entries, const uint32_t *left_list)
{
	int i;

	printf("nodelist notification called \n");
	printf("  ring id            = " CS_PRI_RING_ID "\n", ring_id.nodeid, ring_id.seq);
	printf("  num members        = %" PRIu32 " ", member_list_entries);

	for (i=0; i<member_list_entries; i++) {
		printf(" " CS_PRI_NODE_ID, member_list[i]);
	}
	printf("\n");

	printf("  num joined members = %" PRIu32 " ", joined_list_entries);
	for (i=0; i<joined_list_entries; i++) {
		printf(" " CS_PRI_NODE_ID, joined_list[i]);
	}
	printf("\n");

	printf("  num left members   = %" PRIu32 " ", left_list_entries);
	for (i=0; i<left_list_entries; i++) {
		printf(" " CS_PRI_NODE_ID, left_list[i]);
	}
	printf("\n");

}

int main(int argc, char *argv[])
{
	int quorate;
	quorum_model_v1_data_t model_data;
	uint32_t quorum_type;
	int err;

	memset(&model_data, 0, sizeof(model_data));
	model_data.quorum_notify_fn = quorum_notification_fn;
	model_data.nodelist_notify_fn = nodelist_notification_fn;

	if ( (err=quorum_model_initialize (&g_handle, QUORUM_MODEL_V1,
	    (quorum_model_data_t *)&model_data, &quorum_type, NULL)) != CS_OK) {
		fprintf(stderr, "quorum_initialize FAILED: %d\n", err);
		exit(1);
	}

	if ( (err=quorum_trackstart(g_handle, CS_TRACK_CHANGES)) != CS_OK)
		fprintf(stderr, "quorum_trackstart FAILED: %d\n", err);

	if ( (err=quorum_getquorate(g_handle, &quorate)) != CS_OK)
		fprintf(stderr, "quorum_getquorate FAILED: %d\n", err);
	else {
		printf("quorate   %d\n", quorate);
	}

	printf("Waiting for quorum events, press ^C to finish\n");
	printf("-------------------\n");

	if (quorum_dispatch(g_handle, CS_DISPATCH_BLOCKING) != CS_OK) {
		fprintf(stderr, "Error from quorum_dispatch\n");
		return -1;
	}

	return 0;
}
