/*
 * Test program for event service
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <malloc.h>
#include "ais_types.h"
#include "ais_evt.h"


void testresult (SaErrorT result, SaErrorT expected, int test_no)
{
	if (result == expected) {
		printf ("Test %d passed\n", test_no);
	} else {
		printf ("ERROR: Test %d FAILED (expected %d got %d)\n", test_no, expected, result);
	}
}

SaVersionT version1 = { 'A', 0x01, 0x01 };
SaVersionT version2 = { 'a', 0x01, 0x01 };
SaVersionT version3 = { 'b', 0x01, 0x01 };
SaVersionT version4 = { 'a', 0x02, 0x01 };
SaVersionT version5 = { 'a', 0xff, 0x01 };
SaVersionT version6 = { 'a', 0x01, 0xff };
SaVersionT version7 = { 'A', 0xff, 0xff };
SaVersionT version8 = { 'B', 0xff, 0xff };

struct version_test {
	SaVersionT	*version;
	SaErrorT	result;
};

struct version_test versions[] = {
	{ &version1, SA_OK },
	{ &version2, SA_OK },
	{ &version3, SA_ERR_VERSION },
	{ &version4, SA_ERR_VERSION},
//	{ &version5, SA_OK },
	//{ &version6, SA_OK },
	//{ &version7, SA_OK },
	{ &version8, SA_ERR_VERSION},
	{ 0, SA_ERR_VERSION}
};

int version_size = sizeof(versions) / sizeof(struct version_test);

void event_callback( SaEvtSubscriptionIdT subscriptionId,
		const SaEvtEventHandleT eventHandle,
		const SaSizeT eventDataSize);

SaEvtCallbacksT callbacks = {
	0,
	event_callback
};

char channel[256] = "TESTEVT_CHANNEL";
SaEvtSubscriptionIdT subscription_id = 0xabcdef;
unsigned long long test_ret_time = 30000000000ULL; /* 30 seconds */


/*
 * event data
 */

long *exp_data;
#define DATA_SIZE 2048 /* default data size */
#define LCOUNT DATA_SIZE/sizeof(long)
	
void test_initialize (void) {
	int result;
	SaEvtHandleT handle;
	int i;


	/*
	 * version check tests
	 */
	printf("Test lib version check on initlialize\n");
	for (i=0; i < version_size; i++) {
		result = saEvtInitialize (&handle, 0, versions[i].version);
		testresult (result, versions[i].result, i);
		if (result == SA_OK) {
			saEvtFinalize(handle);
		}
	}


}

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

void
test_channel()
{
	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	int result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER |
		SA_EVT_CHANNEL_SUBSCRIBER |
		SA_EVT_CHANNEL_CREATE;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);
	/*
	 * Channel open/close test
	 */
	printf("Test Channel operations:\n");

	printf("Channel open:\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);

	if (result != SA_OK) {
		printf("ERROR: Event Initialize result: %d\n", result);
		return;
	}

	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);


	if (result != SA_OK) {
		printf("ERROR: channel open result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Event Finalize result: %d\n", result);
		return;
	}

	printf("Channel close:\n");
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		printf("ERROR: channel close result: %d\n", result);
		return;
	}

	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		printf("ERROR: Event Finalize result: %d\n", result);
		return;
	}
	
	/*
	 * Test channel subscribe
	 */
	printf("Channel subscribe:\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		printf("ERROR: Event Initialize result: %d\n", result);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		printf("ERROR: channel open result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		printf("ERROR: event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) 
			printf("ERROR: Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}


	printf("Channel unsubscribe:\n");

	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) {
		printf("ERROR: event unsubscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) 
			printf("ERROR: Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}
	result = saEvtChannelClose(channel_handle);
	if (result != SA_OK) 
		printf("ERROR: Channel close result: %d\n", result);
	result = saEvtFinalize(handle);
	if (result != SA_OK) 
		printf("ERROR: Finalize result: %d\n", result);

	/*
	 * Test channel subscribe with no close
	 */
	printf("Channel subscribe with no close at end:\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		printf("ERROR: Event Initialize result: %d\n", result);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		printf("ERROR: channel open result: %d\n", result);
		result = saEvtFinalize(handle);
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		printf("ERROR: event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
	}

	result = saEvtFinalize(handle);
	if (result != SA_OK) {
		printf("ERROR: Finalize failed\n");
		return;
	}

	/*
	 * Test multiple subscriptions
	 */
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		printf("ERROR: Event Initialize result: %d\n", result);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		printf("ERROR: channel open result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		printf("ERROR: First event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) 
			printf("ERROR: Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id+1);

	if (result != SA_OK) {
		printf("ERROR: second event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) 
			printf("ERROR: Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}

	/*
	 * Test duplicate subscription
	 */
	printf("Duplicate subscription\n");

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_ERR_EXIST) {
		printf("ERROR: First event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		result = saEvtFinalize(handle);
		return;
	}

	/*
	 * Test unsubscribe non-existent sub_id
	 */
	printf("Unsubscribe non-existent sub_id\n");

	result = saEvtEventUnsubscribe(channel_handle, subscription_id+2);
	if (result != SA_ERR_INVALID_PARAM) {
		printf("ERROR: event unsubscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) 
			printf("ERROR: Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}

	printf("Unsubscribe from both, close, and finalize\n");
	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) 
		printf("ERROR: first event unsubscribe result: %d\n", result);
	result = saEvtEventUnsubscribe(channel_handle, subscription_id+1);
	if (result != SA_OK) 
		printf("ERROR: second event unsubscribe result: %d\n", result);
	result = saEvtChannelClose(channel_handle);
	if (result != SA_OK) 
		printf("ERROR: Channel close result: %d\n", result);
	result = saEvtFinalize(handle);
	if (result != SA_OK) 
		printf("ERROR: Finalize result: %d\n", result);
	printf("Done\n");

}

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
SaNameT test_pub_name = {13, "Test Pub Name"};
#define TEST_PRIORITY 2

SaEvtEventPatternArrayT evt_pat_set_array = {
	patterns,
	sizeof(patterns)/sizeof(SaEvtEventPatternT)
};

char event_data[1000];
#define EVENT_DATA_SIZE 1000

SaEvtEventIdT event_id = 0;
SaUint8T priority;
SaTimeT retention_time = 0ULL;
SaNameT publisher_name = {0, {0}};
SaSizeT event_data_size;

void
test_event()
{
	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtEventHandleT event_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;

	SaTimeT publish_time;
	struct pollfd pfd;
	int nfd;
	int fd;
	int timeout = 5000;


	
	int result;
	int i;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER|SA_EVT_CHANNEL_SUBSCRIBER;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);

	printf("Test Event operations:\n");

	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		printf("ERROR: Event Initialize result: %d\n", result);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		printf("ERROR: channel open result: %d\n", result);
		goto evt_fin;
	}

	/*
	 * Allocate an event
	 */
	printf("Event allocation\n");
	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		printf("ERROR: event Allocate result: %d\n", result);
		goto evt_close;
	}

	printf("Get event attributes(1)\n");
	result = saEvtEventAttributesGet(event_handle,
			0,	/* patterns */
			0,	/* priority */
			0,	/* retention time */
			0,	/* publisher name */
			0,	/* publish time */
			0	/* event_id */
			);
	if (result != SA_OK) {
		printf("ERROR: event get attr result(1): %d\n", result);
		goto evt_free;
	}

	/*
	 * Get event attributes, this time supply pointers.
	 * validate the default values.
	 */
	printf("Get event attributes(2)\n");
	evt_pat_get_array.patternsNumber = 4;
	result = saEvtEventAttributesGet(event_handle,
			&evt_pat_get_array,	/* patterns */
			&priority,	/* priority */
			&retention_time,	/* retention time */
			&publisher_name,	/* publisher name */
			&publish_time,	/* publish time */
			&event_id	/* event_id */
			);
	if (result != SA_OK) {
		printf("ERROR: event get attr result(2): %d\n", result);
		goto evt_free;
	}
	if (evt_pat_get_array.patternsNumber != 0) {
		printf("ERROR: pattern array count not zero: %d\n", 
					evt_pat_get_array.patternsNumber);
	}
	if (priority != SA_EVT_LOWEST_PRIORITY) {
		printf("ERROR: priority not lowest: 0x%x\n", priority);
	}
	if (retention_time != 0) {
		printf("ERROR: retention time not zero: %0llx\n", retention_time);
	}
	if (publisher_name.length != 0) {
		printf("ERROR: publisher name not null: %s\n", publisher_name.value);
	}
	if (event_id != 0) {
		printf("ERROR: event id not zero: 0x%llx\n", event_id);
	}


	/*
	 * Set some attributes, then read them back
	 */
	printf("Set event attributes(1)\n");
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			test_ret_time,
			&test_pub_name);
	if (result != SA_OK) {
		printf("ERROR: event set attr result(1): %d\n", result);
		goto evt_free;
	}

	printf("Get event attributes(3)\n");
	evt_pat_get_array.patternsNumber = 4;
	result = saEvtEventAttributesGet(event_handle,
			&evt_pat_get_array,	/* patterns */
			&priority,		/* priority */
			&retention_time,	/* retention time */
			&publisher_name,	/* publisher name */
			&publish_time,		/* publish time */
			&event_id		/* event_id */
			);
	if (result != SA_OK) {
		printf("ERROR: event get attr result(2): %d\n", result);
		goto evt_free;
	}
	if (evt_pat_get_array.patternsNumber != 4) {
		printf("ERROR: pattern array count not 4: %d\n", 
					evt_pat_get_array.patternsNumber);
	}
	for (i = 0; i < evt_pat_get_array.patternsNumber; i++) {
		if (evt_pat_get_array.patterns[i].patternSize !=
				evt_pat_set_array.patterns[i].patternSize) {
			printf("ERROR: pattern %d count not equal g=%d, s=%d\n",
				i,
				evt_pat_get_array.patterns[i].patternSize,
				evt_pat_set_array.patterns[i].patternSize);
			printf("ERROR: pattern %d content g=\"%s\", s=\"%s\"\n",
				i,
				evt_pat_get_array.patterns[i].pattern,
				evt_pat_set_array.patterns[i].pattern);
		} else {
			if (memcmp(evt_pat_get_array.patterns[i].pattern,
			    evt_pat_set_array.patterns[i].pattern,
			    evt_pat_get_array.patterns[i].patternSize) != 0){
				printf(
				 "ERROR: pattern %d don't match g=\"%s\", s=\"%s\"\n",
				  i,
				  evt_pat_get_array.patterns[i].pattern,
				  evt_pat_set_array.patterns[i].pattern);
			}
		}
	}

	if (priority != TEST_PRIORITY) {
		printf("ERROR: priority: e=0x%x a=0x%x\n", 
				TEST_PRIORITY, priority);
	}
	if (retention_time != test_ret_time) {
		printf("ERROR: retention: e=0x%llx a=0x%llx\n", 
				test_ret_time, retention_time);
	}
	if (publisher_name.length != test_pub_name.length) {
		printf("ERROR: publisher name length: e=%d, a=%d\n", 
		test_pub_name.length,
		publisher_name.length); 
	} else {
		if (memcmp(publisher_name.value, test_pub_name.value,
					publisher_name.length) != 0) {
			printf("ERROR: publisher name content: e=%s, a=%s\n", 
			test_pub_name.value,
			publisher_name.value); 
		}
	}

	if (event_id != 0) {
		printf("ERROR: event id not zero: 0x%llx\n", event_id);
	}
	/*
	 * Event attr patterns: short reads/long reads
	 */
	/*    TODO     */

	/*
	 * event user data
	 */
	printf("Get event data(1)\n");
	result = saEvtEventDataGet(event_handle, 0, 0);
	if (result != SA_OK) {
		printf("ERROR: Get event data(1) result: %d\n", result);
	}
	printf("Get event data(2)\n");
	result = saEvtEventDataGet(event_handle, event_data, 0);
	if (result != SA_OK) {
		printf("ERROR: Get event data(2) result: %d\n", result);
	}
	printf("Get event data(3)\n");
	event_data_size = EVENT_DATA_SIZE;
	result = saEvtEventDataGet(event_handle, 0, &event_data_size);
	if (result != SA_OK) {
		printf("ERROR: Get event data(3) result: %d\n", result);
	}
	printf("Get event data(4)\n");
	event_data_size = EVENT_DATA_SIZE;
	result = saEvtEventDataGet(event_handle, event_data, &event_data_size);
	if (result != SA_OK) {
		printf("ERROR: Get event data(4) result: %d\n", result);
	}
	printf("Get event data(5)\n");
	event_data_size = 1;
	result = saEvtEventDataGet(event_handle, event_data, &event_data_size);
	if (result != SA_OK) {
		printf("ERROR: Get event data(5) result: %d\n", result);
	}

	printf("Free event(1)\n");
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		printf("ERROR: event free result: %d\n", result);
	}

	/*
	 * Test publication.
	 */

	printf("Publish with no patterns set\n");

	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		printf("ERROR: event Allocate result: %d\n", result);
		goto evt_close;
	}

	result = saEvtEventPublish(event_handle, 0, 0, &event_id);
	if (result != SA_ERR_INVALID_PARAM) {
		printf("ERROR: event Publish result(1): %d\n", result);
		goto evt_close;
	}

	/*
	 * Publish with pattens
	 */
	printf("Publish with patterns set\n");

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		printf("ERROR: event subscribe result: %d\n", result);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) 
			printf("ERROR: Channel close result: %d\n", result);
		result = saEvtFinalize(handle);
		if (result != SA_OK) 
			printf("ERROR: Finalize result: %d\n", result);
		return;
	}

	retention_time = 0ULL;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		printf("ERROR: event set attr result(2): %d\n", result);
		goto evt_free;
	}

	/*
	 * Generate event data
	 */
	exp_data = malloc(DATA_SIZE);
	for (i = 0; i < LCOUNT; i++) {
		exp_data[i] = lrand48();
	}
	event_data_size = DATA_SIZE;

	/*
	 * Send it
	 */
	result = saEvtEventPublish(event_handle, exp_data,  DATA_SIZE, 
								&event_id);
	if (result != SA_OK) {
		printf("ERROR: event Publish result(2): %d\n", result);
		goto evt_close;
	}

	/*
	 * See if we got the event
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		printf("ERROR: saEvtSelectionObject get %d\n", result);
		/* error */
		return;
	}
	pfd.fd = fd;
	pfd.events = POLLIN;
	nfd = poll(&pfd, 1, timeout);
	if (nfd <= 0) {
		printf("ERROR: poll fds %d\n", nfd);
		if (nfd < 0) {
			perror("ERROR: poll error");
		}
		/* Error */
		return;
	}

	result = saEvtDispatch(handle, SA_DISPATCH_ONE);
	if (result != SA_OK) {
		printf("ERROR: saEvtDispatch %d\n", result);
		/* error */
		return;
	}



	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		printf("ERROR: event free result: %d\n", result);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		printf("ERROR: channel close result: %d\n", result);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		printf("ERROR: Event Finalize result: %d\n", result);
	}
	printf("Done\n");

}
void 
event_callback(SaEvtSubscriptionIdT my_subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT my_event_data_size)
{
	SaErrorT result;
	SaUint8T my_priority;
	SaTimeT my_retention_time;
	SaNameT my_publisher_name = {0, {0}};
	SaTimeT my_publish_time;
	SaEvtEventIdT my_event_id;
	int i;
	long *act_data;
	SaSizeT	data_size;

	printf("event_callback called\n");
	if (my_subscription_id != subscription_id) {
		printf("ERROR: sub ID: e=%lx, a=%lx\n", 
				subscription_id, my_subscription_id);
	}
	if (my_event_data_size != event_data_size) {
		printf("ERROR: event data size e=%d, a=%d\n", 
				event_data_size,
				my_event_data_size);
	}

	evt_pat_get_array.patternsNumber = 4;
	result = saEvtEventAttributesGet(event_handle,
			&evt_pat_get_array,	/* patterns */
			&my_priority,		/* priority */
			&my_retention_time,	/* retention time */
			&my_publisher_name,	/* publisher name */
			&my_publish_time,	/* publish time */
			&my_event_id		/* event_id */
			);
	if (result != SA_OK) {
		printf("ERROR: event get attr result(2): %d\n", result);
		goto evt_free;
	}

	if (my_event_id != event_id) {
		printf("ERROR: Call back event ID error: e=%llx, a=%llx\n",
				event_id, my_event_id);
	}

	if (evt_pat_get_array.patternsNumber != 4) {
		printf("ERROR: pattern array count not 4: %d\n", 
					evt_pat_get_array.patternsNumber);
	}
	for (i = 0; i < evt_pat_get_array.patternsNumber; i++) {
		if (evt_pat_get_array.patterns[i].patternSize !=
				evt_pat_set_array.patterns[i].patternSize) {
			printf("ERROR: pattern %d count not equal g=%d, s=%d\n",
				i,
				evt_pat_get_array.patterns[i].patternSize,
				evt_pat_set_array.patterns[i].patternSize);
			printf("ERROR: pattern %d content g=\"%s\", s=\"%s\"\n",
				i,
				evt_pat_get_array.patterns[i].pattern,
				evt_pat_set_array.patterns[i].pattern);
		} else {
			if (memcmp(evt_pat_get_array.patterns[i].pattern,
			    evt_pat_set_array.patterns[i].pattern,
			    evt_pat_get_array.patterns[i].patternSize) != 0){
				printf(
				 "ERROR: pattern %d don't match g=\"%s\", s=\"%s\"\n",
				  i,
				  evt_pat_get_array.patterns[i].pattern,
				  evt_pat_set_array.patterns[i].pattern);
			}
		}
	}

	if (priority != my_priority) {
		printf("ERROR: priority: e=0x%x a=0x%x\n", 
				priority, my_priority);
	}
	if (retention_time != my_retention_time) {
		printf("ERROR: retention: e=0x%llx a=0x%llx\n", 
				retention_time, my_retention_time);
	}
	if (publisher_name.length != my_publisher_name.length) {
		printf("ERROR: publisher name length: e=%d, a=%d\n", 
		publisher_name.length,
		my_publisher_name.length); 
	} else {
		if (memcmp(publisher_name.value, my_publisher_name.value,
					publisher_name.length) != 0) {
			printf("ERROR: publisher name content: e=%s, a=%s\n", 
			publisher_name.value,
			my_publisher_name.value); 
		}
	}

	act_data = malloc(my_event_data_size);
	memset(act_data, 0, my_event_data_size);
	data_size = my_event_data_size;
	result = saEvtEventDataGet(event_handle, act_data, &data_size);
	if (result != SA_OK) {
		printf("ERROR: event data get result: %d\n", result);
		goto dat_free;
	}
	if (data_size != event_data_size) {
		printf("ERROR: Data size: e=%d a=%d\n", 
				event_data_size, data_size);
	}
	for (i = 0; i < (data_size/sizeof(long)); i++) {
		if (act_data[i] != exp_data[i]) {
			printf("ERROR: Event Data e=%lx a=%lx at index %d\n",
					exp_data[i], act_data[i], i);
			break;
		}
	}

dat_free:
	free(act_data);
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		printf("ERROR: event free result: %d\n", result);
	}
}


int main (void)
{
	test_initialize ();
	test_channel();
	test_event();

	return (0);
}
