/*
 * Test program for event service
 */

#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#ifndef OPENAIS_SOLARIS
#include <stdint.h>
#include <getopt.h>
#else
#include <sys/types.h>
#endif
#include <stdlib.h>
#include <sys/time.h>
#include "saAis.h"
#include "saEvt.h"

// #define EVENT_SUBSCRIBE

#ifdef OPENAIS_SOLARIS
#define timersub(a, b, result)						\
    do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;		\
	if ((result)->tv_usec < 0) {					\
	    --(result)->tv_sec;						\
	    (result)->tv_usec += 1000000;				\
	}								\
    } while (0)
#endif

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
unsigned long long ret_time = 0000000000ULL; /* 0 seconds */
char pubname[256] = "Test Pub Name";


#define _patt1 "Filter pattern 1"
#define patt1 (SaUint8T *) _patt1
#define patt1_size sizeof(_patt1)

#define _patt2 "Filter pattern 2"
#define patt2 (SaUint8T *) _patt2
#define patt2_size sizeof(_patt2)

#define _patt3 "Filter pattern 3"
#define patt3 (SaUint8T *) _patt3
#define patt3_size sizeof(_patt3)

#define _patt4 "Filter pattern 4"
#define patt4 (SaUint8T *) _patt4
#define patt4_size sizeof(_patt4)

SaEvtEventFilterT filters[] = {
	{SA_EVT_PREFIX_FILTER, {patt1_size, patt1_size, patt1}},
	{SA_EVT_SUFFIX_FILTER, {patt2_size, patt2_size, patt2}},
	{SA_EVT_EXACT_FILTER, {patt3_size, patt3_size, patt3}},
	{SA_EVT_PASS_ALL_FILTER, {patt4_size, patt4_size, patt4}}
};

SaEvtEventFilterArrayT subscribe_filters = {
	sizeof(filters)/sizeof(SaEvtEventFilterT),
	filters 
};


	SaUint8T pat0[100];
	SaUint8T pat1[100];
	SaUint8T pat2[100];
	SaUint8T pat3[100];
	SaUint8T pat4[100];
	SaEvtEventPatternT evt_patts[5] = {
		{100, 100, pat0},
		{100, 100, pat1},
		{100, 100, pat2},
		{100, 100, pat3},
		{100, 100, pat4}};
	SaEvtEventPatternArrayT	evt_pat_get_array = { 100, 0, evt_patts };

SaEvtEventPatternT patterns[] = {
	{patt1_size, patt1_size, patt1},
	{patt2_size, patt2_size, patt2},
	{patt3_size, patt3_size, patt3},
	{patt4_size, patt4_size, patt4}
};
SaNameT test_pub_name;
#define TEST_PRIORITY 2

SaEvtEventPatternArrayT evt_pat_set_array = {
	sizeof(patterns)/sizeof(SaEvtEventPatternT),
	sizeof(patterns)/sizeof(SaEvtEventPatternT),
	patterns
};

char user_data_file[256];
char  user_data[100000];
int user_data_size = 50000;

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

void
test_pub()
{
	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtEventHandleT event_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	uint64_t test_retention;
	SaSelectionObjectT fd;
	int i;
	struct timeval tv1, tv2, tv_elapsed;
	int write_count = 10000;
	int write_size = user_data_size;


	SaEvtEventIdT event_id;
#ifdef EVENT_SUBSCRIBE
	struct pollfd pfd;
	int nfd;
	int timeout = 1000;
#endif


	
	int result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER |
#ifdef EVENT_SUBSCRIBE
		SA_EVT_CHANNEL_SUBSCRIBER |
#endif
		SA_EVT_CHANNEL_CREATE;
	strcpy((char *)channel_name.value, channel);
	channel_name.length = strlen(channel);


	result = saEvtInitialize (&handle, &callbacks, &version);
	if (result != SA_AIS_OK) {
		printf("Event Initialize result: %d\n", result);
		exit(1);
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 
			SA_TIME_MAX, &channel_handle);
	if (result != SA_AIS_OK) {
		printf("channel open result: %d\n", result);
		goto evt_fin;
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
		printf("event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_AIS_OK) 
			printf("Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_AIS_OK) 
			printf("Finalize result: %d\n", result);
		return;
	}
#endif
	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_AIS_OK) {
		printf("event Allocate result: %d\n", result);
		goto evt_free;
	}

	strcpy((char *)test_pub_name.value, pubname);
	test_pub_name.length = strlen(pubname);
	test_retention = ret_time;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			test_retention,
			&test_pub_name);
	if (result != SA_AIS_OK) {
		printf("event set attr result(2): %d\n", result);
		goto evt_free;
	}

	gettimeofday (&tv1, NULL);
	for (i = 0; i < write_count; i++) {
		result = saEvtEventPublish(event_handle, user_data, 
						write_size, &event_id);
		if (result != SA_AIS_OK) {
			printf("event Publish result(2): %d\n", result);
			goto evt_close;
		}
	}
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d Writes ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ",
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n",
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (
		tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));

exit (1);
	printf("Published event ID: %llx\n", (unsigned long long)event_id);

	/*
	 * See if we got the event
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_AIS_OK) {
		printf("saEvtSelectionObject get %d\n", result);
		/* error */
		return;
	}
#ifdef EVENT_SUBSCRIBE
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
		printf("saEvtDispatch %d\n", result);
		goto evt_fin;
	}
#endif


	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_AIS_OK) {
		printf("event free result: %d\n", result);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_AIS_OK) {
		printf("channel close result: %d\n", result);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_AIS_OK) {
		printf("Event Finalize result: %d\n", result);
	}
	printf("Done\n");

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
	printf("event data size %llu\n", (unsigned long long)event_data_size);

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
		printf("event get attr result(2): %d\n", result);
		goto evt_free;
	}
	printf("pattern array count: %llu\n",
		(unsigned long long)evt_pat_get_array.patternsNumber);
	for (i = 0; i < evt_pat_get_array.patternsNumber; i++) {
		printf( "pattern %d =\"%s\"\n", i,
				  evt_pat_get_array.patterns[i].pattern);
	}

	printf("priority: 0x%x\n", priority);
	printf("retention: %llx\n", (unsigned long long)retention_time);
	printf("publisher name content: \"%s\"\n", publisher_name.value); 
	printf("event id: %llx\n", (unsigned long long)event_id);
evt_free:
	result = saEvtEventFree(event_handle);
	printf("event free result: %d\n", result);
}


int main (int argc, char **argv)
{
	static const char opts[] = "c:i:t:n:x:u:";

	int pub_count = 1;
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

	while (pub_count--) {
		test_pub();
	}

	return 0;
}
