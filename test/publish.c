/*
 * Test program for event service
 */

#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/time.h>
#include "ais_types.h"
#include "saEvt.h"

// #define EVENT_SUBSCRIBE

extern int get_sa_error(SaAisErrorT, char *, int);
char result_buf[256];
int result_buf_len = sizeof(result_buf);

static int pub_count = 1;
static int wait_time = -1;

SaVersionT version = { 'B', 0x01, 0x01 };

void event_callback( SaEvtSubscriptionIdT subscriptionId,
		const SaEvtEventHandleT eventHandle,
		const SaSizeT eventDataSize);

SaEvtCallbacksT callbacks = {
	0,
	event_callback
};


char channel[256] = "EVENT_TEST_CHANNEL";
unsigned int subscription_id = 0xfedcba98;
unsigned long long ret_time = 30000000000ULL; /* 30 seconds */
char pubname[256] = "Test Pub Name";

	
#define patt1 "Filter pattern 1"
#define patt1_size sizeof(patt1)

#define patt2 "Filter pattern 2"
#define patt2_size sizeof(patt2)

#define patt3 "Filter pattern 3"
#define patt3_size sizeof(patt3)

#define patt4 "Filter pattern 4"
#define patt4_size sizeof(patt4)


SaEvtEventFilterT filters[] = {
	{SA_EVT_PREFIX_FILTER, {patt1, patt1_size}},
	{SA_EVT_SUFFIX_FILTER, {patt2, patt2_size}},
	{SA_EVT_EXACT_FILTER, {patt3, patt3_size}},
	{SA_EVT_PASS_ALL_FILTER, {patt4, patt4_size}}
};

SaEvtEventFilterArrayT subscribe_filters = {
	filters, 
	sizeof(filters)/sizeof(SaEvtEventFilterT)
};


	SaUint8T pat0[100];
	SaUint8T pat1[100];
	SaUint8T pat2[100];
	SaUint8T pat3[100];
	SaUint8T pat4[100];
	SaEvtEventPatternT evt_patts[5] = {
		{pat0, 100},
		{pat1, 100},
		{pat2, 100},
		{pat3, 100},
		{pat4, 100}};
	SaEvtEventPatternArrayT	evt_pat_get_array = { evt_patts, 0 };

SaEvtEventPatternT patterns[] = {
	{patt1, patt1_size},
	{patt2, patt2_size},
	{patt3, patt3_size},
	{patt4, patt4_size}
};
SaNameT test_pub_name;
#define TEST_PRIORITY 2

SaEvtEventPatternArrayT evt_pat_set_array = {
	patterns,
	sizeof(patterns)/sizeof(SaEvtEventPatternT)
};

char user_data_file[256];
char  user_data[65536];
int user_data_size = 0;

uint64_t clust_time_now(void)
{
	struct timeval tv;
	uint64_t time_now;

	if (gettimeofday(&tv, 0)) {
		return 0ULL;
	}

	time_now = (uint64_t)(tv.tv_sec) * 1000000000ULL;
	time_now += (uint64_t)(tv.tv_usec) * 1000ULL;

	return time_now;
}

int
test_pub()
{
	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtEventHandleT event_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	uint64_t test_retention;
	int fd;
	int i;

	SaEvtEventIdT event_id;
#ifdef EVENT_SUBSCRIBE
	struct pollfd pfd;
	int nfd;
	int timeout = 1000;
#endif


	
	SaAisErrorT result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER |
#ifdef EVENT_SUBSCRIBE
		SA_EVT_CHANNEL_SUBSCRIBER |
#endif
		SA_EVT_CHANNEL_CREATE;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);


	result = saEvtInitialize (&handle, &callbacks, &version);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("Event Initialize result: %s\n", result_buf);
		exit(result);
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("channel open result: %s\n", result_buf);
		exit(result);
	}

	/*
	 * Publish with pattens
	 */
	printf("Publish\n");

#ifdef EVENT_SUBSCRIBE
	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("event subscribe result: %s\n", result_buf);
		exit(result);
	}
#endif
	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("event Allocate result: %s\n", result_buf);
		exit(result);
	}

	strcpy(test_pub_name.value, pubname);
	test_pub_name.length = strlen(pubname);
	test_retention = ret_time;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			test_retention,
			&test_pub_name);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("event set attr result(2): %s\n", result_buf);
		exit(result);
	}

	for (i = 0; i < pub_count; i++) {
	result = saEvtEventPublish(event_handle, user_data, 
						user_data_size, &event_id);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("event Publish result(2): %s\n", result_buf);
		exit(result);
	}

	printf("Published event ID: 0x%llx\n", (unsigned long long)event_id);
	}

	/*
	 * See if we got the event
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("saEvtSelectionObject get %s\n", result_buf);
		/* error */
		exit(result);
	}
#ifdef EVENT_SUBSCRIBE

	for (i = 0; i < pub_count; i++) {
	pfd.fd = fd;
	pfd.events = POLLIN;
	nfd = poll(&pfd, 1, timeout);
	if (nfd <= 0) {
		printf("poll fds %d\n", nfd);
		if (nfd < 0) {
			perror("poll error");
		}
		goto evt_free;
	}

	printf("Got poll event\n");
	result = saEvtDispatch(handle, SA_DISPATCH_ONE);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("saEvtDispatch %s\n", result_buf);
		exit(result);
	}
	}
#endif


	/*
	 * Test cleanup
	 */
	result = saEvtEventFree(event_handle);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("event free result: %s\n", result_buf);
		exit(result);
	}

	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("channel close result: %s\n", result_buf);
		exit(result);
	}
	result = saEvtFinalize(handle);

	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("Event Finalize result: %s\n", result_buf);
		exit(result);
	}
	printf("Done\n");
	return 0;

}

void 
event_callback( SaEvtSubscriptionIdT subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT event_data_size)
{
	SaAisErrorT result;
	SaUint8T priority;
	SaTimeT retention_time;
	SaNameT publisher_name = {0, {0}};
	SaTimeT publish_time;
	SaEvtEventIdT event_id;
	int i;

	printf("event_callback called\n");
	printf("sub ID: %x\n", subscription_id);
	printf("event_handle %llx\n", (unsigned long long)event_handle);
	printf("event data size %d\n", event_data_size);

	evt_pat_get_array.patternsNumber = 4;
	result = saEvtEventAttributesGet(event_handle,
			&evt_pat_get_array,	/* patterns */
			&priority,		/* priority */
			&retention_time,	/* retention time */
			&publisher_name,	/* publisher name */
			&publish_time,		/* publish time */
			&event_id		/* event_id */
			);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("event get attr result(2): %s\n", result_buf);
		goto evt_free;
	}
	printf("pattern array count: %d\n", evt_pat_get_array.patternsNumber);
	for (i = 0; i < evt_pat_get_array.patternsNumber; i++) {
		printf( "pattern %d =\"%s\"\n", i,
				  evt_pat_get_array.patterns[i].pattern);
	}

	printf("priority: 0x%x\n", priority);
	printf("retention: 0x%llx\n", (unsigned long long)retention_time);
	printf("publisher name content: \"%s\"\n", publisher_name.value); 
	printf("event id: 0x%llx\n", (unsigned long long)event_id);
evt_free:
	result = saEvtEventFree(event_handle);
	get_sa_error(result, result_buf, result_buf_len);
	printf("event free result: %s\n", result_buf);
}


int main (int argc, char **argv)
{
	static const char opts[] = "c:i:t:n:x:u:w:";

	int ret;
	int option;

	while (1) {
		option = getopt(argc, argv, opts);
		if (option == -1) 
			break;

		switch (option) {
		case 'u': {
			int fd;
			int sz;

			strcpy(user_data_file, optarg);
			fd = open(user_data_file, O_RDONLY);
			if (fd < 0) {
				printf("Can't open user data file %s\n",
						user_data_file);
				exit(1);
			}
			sz = read(fd, user_data, 65536);
			if (sz < 0) {
				perror("subscription\n");
				exit(1);
			}
			close(fd);
			user_data_size = sz;
			break;
		}

		case 'c':
			strcpy(channel, optarg);
			break;
		case 'n':
			strcpy(pubname, optarg);
			break;
		case 'i':
			subscription_id = 
				(unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'w':
			wait_time = 
				(unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 't':
			ret_time = strtoull(optarg, NULL, 0);
			ret_time *= 1000000000;
			break;
		case 'x':
			pub_count = strtoul(optarg, NULL, 0);
			break;
		default:
			printf("invalid arg: \"%s\"\n", optarg);
			return 1;
		}
	}
	do {
		ret = test_pub();
		if (ret != 0) {
			exit(ret);
		}
		if (wait_time < 0) {
			break;
		}
		sleep(wait_time);
	} while(1);
	return 0;
}
