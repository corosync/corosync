#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "../include/evs.h"

char *delivery_string;

void evs_deliver_fn (struct in_addr source_addr, void *msg, int msg_len)
{
	char *buf = msg;

//	buf += 100000;
//	printf ("Delivery callback\n");
	printf ("API '%s' msg '%s'\n", delivery_string, buf);
}

void evs_confchg_fn (
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries)
{
	int i;

	printf ("CONFIGURATION CHANGE\n");
	printf ("--------------------\n");
	printf ("New configuration\n");
	for (i = 0; i < member_list_entries; i++) {
		printf ("%s\n", inet_ntoa (member_list[i]));
	}
	printf ("Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		printf ("%s\n", inet_ntoa (left_list[i]));
	}
	printf ("Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		printf ("%s\n", inet_ntoa (joined_list[i]));
	}
}

evs_callbacks_t callbacks = {
	evs_deliver_fn,
	evs_confchg_fn
};

struct evs_group groups[3] = {
	{ "key1" },
	{ "key2" },
	{ "key3" }
};

char buffer[1000];
struct iovec iov = {
	.iov_base = buffer,
	.iov_len = sizeof (buffer)
};

int main (void)
{
	unsigned int handle;
	evs_error_t result;
	int i = 0;

	result = evs_initialize (&handle, &callbacks);
	printf ("Init result %d\n", result);
	result = evs_join (&handle, groups, 3);
	printf ("Join result %d\n", result);
	result = evs_leave (&handle, &groups[0], 1);
	printf ("Leave result %d\n", result);
	delivery_string = "evs_mcast_joined";

	/*
	 * Demonstrate evs_mcast_joined
	 */
	for (i = 0; i < 500; i++) {
		sprintf (buffer, "evs_mcast_joined: This is message %d", i);
try_again_one:
		result = evs_mcast_joined (&handle, EVS_TYPE_AGREED, EVS_PRIO_LOW,
			&iov, 1);
		if (result == EVS_ERR_TRY_AGAIN) {
			goto try_again_one;
		}
		result = evs_dispatch (&handle, EVS_DISPATCH_ALL);
	}

//	result = evs_leave (&handle, &groups[1], 2); This causes an assertion

	/*
	 * Demonstrate evs_mcast_joined
	 */
	delivery_string = "evs_mcast_groups";
	for (i = 0; i < 500; i++) {
		sprintf (buffer, "evs_mcast_groups: This is message %d", i);
try_again_two:
		result = evs_mcast_groups (&handle, EVS_TYPE_AGREED, EVS_PRIO_LOW,
			 &groups[1], 1, &iov, 1);
		if (result == EVS_ERR_TRY_AGAIN) {
			goto try_again_two;
		}
	
		result = evs_dispatch (&handle, EVS_DISPATCH_ALL);
	}
	return (0);
}
