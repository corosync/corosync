/*
 * Copyright (c) 2004 Mark Haverkamp
 * Copyright (c) 2004 Open Source Development Lab
 *
 * All rights reserved.
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
 * - Neither the name of the Open Source Development Lab nor the names of its
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

/*
 * Test program for event service
 * 
 *	test_initialize ();
 *		Version check in saEvtInitialze()
 *
 *	test_channel();
 *		Test basic channel operations.  Open/close/subscribe/unsubscribe
 *
 *	test_event();
 *		Event operations: allocation, free, setting and getting
 *		attributes, publishing.
 *
 *	test_multi_channel1();
 *	test_multi_channel1();
 *	test_multi_channel1();
 *		Test events on multiple channels and multiple subscriptions.
 *
 *	test_retention();
 *		Test event retention times.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <malloc.h>
#include "ais_types.h"
#include "ais_evt.h"

extern int get_sa_error(SaErrorT, char *, int);
char result_buf[256];
int result_buf_len = sizeof(result_buf);


void testresult (SaErrorT result, SaErrorT expected, int test_no)
{
	if (result == expected) {
		printf ("Test %d passed\n", test_no);
	} else {
		get_sa_error(result, result_buf, result_buf_len);
		printf ("ERROR: Test %d FAILED (expected %d got %s)\n", 
						test_no, expected, result_buf);
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
	
/*
 * Test saEvtInitialize and version checking.
 */
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

/*
 * Test channel operations.
 * 1. Open a channel.
 * 2. Close a channel.
 * 3. Channel subscription.
 * 4. Channel unsubscribe
 * 5. unsubscribe/finalize with no channel close.
 * 6. Multiple subscriptions.
 * 7. Duplicate subscription ID.
 * 8. unsubscribe non-exsistent subscription ID.
 * 
 */
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

	printf("       Channel open:\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}

	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);


	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Event Finalize result: %s\n", result_buf);
		}
		return;
	}

	printf("       Channel close:\n");
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result: %s\n", result_buf);
		return;
	}

	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
		return;
	}
	
	/*
	 * Test channel subscribe
	 */
	printf("       Channel subscribe:\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		result = saEvtFinalize(handle);
		if (result != SA_OK)  {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Channel close result: %s\n", result_buf);
		}
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}


	printf("       Channel unsubscribe:\n");

	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event unsubscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Channel close result: %s\n", result_buf);
		}
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}
	result = saEvtChannelClose(channel_handle);
	if (result != SA_OK)  {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Channel close result: %s\n", result_buf);
	}
	result = saEvtFinalize(handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Finalize result: %s\n", result_buf);
	}

	/*
	 * Test channel subscribe with no close
	 */
	printf("       Channel subscribe with no close at end:\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		result = saEvtFinalize(handle);
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
	}

	result = saEvtFinalize(handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Finalize failed\n");
		return;
	}

	/*
	 * Test multiple subscriptions
	 */
	printf("       Multiple subscriptions\n");
	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: First event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Channel close result: %s\n", result_buf);
		}
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id+1);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: second event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Channel close result: %s\n", result_buf);
		}
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}

	/*
	 * Test duplicate subscription
	 */
	printf("       Duplicate subscription\n");

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_ERR_EXIST) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: First event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		result = saEvtFinalize(handle);
		return;
	}

	/*
	 * Test unsubscribe non-existent sub_id
	 */
	printf("       Unsubscribe non-existent sub_id\n");

	result = saEvtEventUnsubscribe(channel_handle, subscription_id+2);
	if (result != SA_ERR_INVALID_PARAM) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event unsubscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Channel close result: %s\n", result_buf);
		}
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}

	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: first event unsubscribe result: %s\n", result_buf);
	}
	result = saEvtEventUnsubscribe(channel_handle, subscription_id+1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: second event unsubscribe result: %s\n", result_buf);
	}
	result = saEvtChannelClose(channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Channel close result: %s\n", result_buf);
	}
	result = saEvtFinalize(handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Finalize result: %s\n", result_buf);
	}
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

/*
 * Test event operations.
 * 
 * 1. Event allocation
 * 2. Get event attributes (no pointers).
 * 3. Get event attributes with pointers.
 * 4. Set/Get event attributes.
 * 5. Get event user data no pointer or count
 * 6. Get event user data with pointer and no count
 * 7. Get event user data with no pointer with a count.
 * 8. Get event user data with pointer and count.
 * 9. Get event user data woth a short count.
 * 10. Free event.
 * 11. Publish with no set patterns.
 * 12. Publish with set patterns and event user data.
 * 
 */
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

	printf("       event_callback called\n");
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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result(2): %s\n", result_buf);
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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event data get result: %s\n", result_buf);
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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}
}

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
	 
	flags = SA_EVT_CHANNEL_PUBLISHER|SA_EVT_CHANNEL_SUBSCRIBER | 
		SA_EVT_CHANNEL_CREATE;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);

	printf("Test Event operations:\n");

	result = saEvtInitialize (&handle, &callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		goto evt_fin;
	}

	/*
	 * Allocate an event
	 */
	printf("       Event allocation\n");
	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate result: %s\n", result_buf);
		goto evt_close;
	}

	printf("       Get event attributes(1)\n");
	result = saEvtEventAttributesGet(event_handle,
			0,	/* patterns */
			0,	/* priority */
			0,	/* retention time */
			0,	/* publisher name */
			0,	/* publish time */
			0	/* event_id */
			);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result(1): %s\n", result_buf);
		goto evt_free;
	}

	/*
	 * Get event attributes, this time supply pointers.
	 * validate the default values.
	 */
	printf("       Get event attributes(2)\n");
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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result(2): %s\n", result_buf);
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
	printf("       Set/get event attributes(1)\n");
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			test_ret_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result(1): %s\n", result_buf);
		goto evt_free;
	}

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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result(2): %s\n", result_buf);
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
	 * event user data
	 */
	printf("       Get event data(1)\n");
	result = saEvtEventDataGet(event_handle, 0, 0);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Get event data(1) result: %s\n", result_buf);
	}
	printf("       Get event data(2)\n");
	result = saEvtEventDataGet(event_handle, event_data, 0);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Get event data(2) result: %s\n", result_buf);
	}
	printf("       Get event data(3)\n");
	event_data_size = EVENT_DATA_SIZE;
	result = saEvtEventDataGet(event_handle, 0, &event_data_size);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Get event data(3) result: %s\n", result_buf);
	}
	printf("       Get event data(4)\n");
	event_data_size = EVENT_DATA_SIZE;
	result = saEvtEventDataGet(event_handle, event_data, &event_data_size);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Get event data(4) result: %s\n", result_buf);
	}
	printf("       Get event data(5)\n");
	event_data_size = 1;
	result = saEvtEventDataGet(event_handle, event_data, &event_data_size);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Get event data(5) result: %s\n", result_buf);
	}

	printf("       Free event(1)\n");
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}

	/*
	 * Test publication.
	 */

	printf("       Publish with no patterns set\n");

	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate result: %s\n", result_buf);
		goto evt_close;
	}

	result = saEvtEventPublish(event_handle, 0, 0, &event_id);
	if (result != SA_ERR_INVALID_PARAM) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result(1): %s\n", result_buf);
		goto evt_close;
	}

	/*
	 * Publish with pattens
	 */
	printf("       Publish with patterns set\n");

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Channel close result: %s\n", result_buf);
		}
		result = saEvtFinalize(handle);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: Finalize result: %s\n", result_buf);
		}
		return;
	}

	retention_time = 0ULL;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result(2): %s\n", result_buf);
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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result(2): %s\n", result_buf);
		goto evt_close;
	}

	/*
	 * See if we got the event
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtSelectionObject get %s\n", result_buf);
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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtDispatch %s\n", result_buf);
		/* error */
		return;
	}



	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result: %s\n", result_buf);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
	}
	printf("Done\n");

}

SaEvtEventIdT event_id1;
SaEvtEventIdT event_id2;
SaEvtEventIdT event_id3;
SaEvtSubscriptionIdT sub1 = 0x101010;
SaEvtSubscriptionIdT sub2 = 0x202020;
static int call_count = 0;

/*
 * Handle call back for multi-test1
 * Checks event ID with subscription ID to make sure that we
 * received an event on the correct subscription.
 */
void 
multi_test_callback1(SaEvtSubscriptionIdT my_subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT my_event_data_size)
{
	SaErrorT result;
	SaUint8T my_priority;
	SaTimeT my_retention_time;
	SaNameT my_publisher_name = {0, {0}};
	SaTimeT my_publish_time;
	SaEvtEventIdT my_event_id;
	SaEvtSubscriptionIdT exp_sub_id;

	printf("       multi_test_callback1 called(%d)\n", ++call_count);

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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result: %s\n", result_buf);
		goto evt_free;
	}

	if (my_event_id == event_id1) {
		exp_sub_id = sub1;
	} else if (my_event_id == event_id2) {
		exp_sub_id = sub2;
	} else if (my_event_id == event_id3) {
		printf("ERROR: Received event 3 but not subscribed\n");
		goto evt_free;
	} else {
		printf("ERROR: Received event %llx but not sent\n", my_event_id);
		goto evt_free;
	}

	if (my_subscription_id != exp_sub_id) {
		printf("ERROR: sub ID: e=%x, a=%x\n", 
				exp_sub_id, my_subscription_id);
		goto evt_free;
	}

	if (evt_pat_get_array.patternsNumber != 1) {
		printf("ERROR: pattern array count not 1: %d\n", 
					evt_pat_get_array.patternsNumber);
	}

evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}
}

/*
 * Test multiple channel operations
 * 1. Test multiple subscriptions on a single channel and receiving
 *    events.
 * 2. Test multiple openings of a single channel and receving events.
 * 3. Test opening of multiple channels and receiving events
 */
void
test_multi_channel1()
{

	SaEvtEventFilterT filt1[1] = {
		{SA_EVT_EXACT_FILTER, {"ChanPat1", 8}},
	};
	SaEvtEventFilterT filt2[1] = {
		{SA_EVT_EXACT_FILTER, {"ChanPat2", 8}},
	};

	SaEvtEventFilterArrayT sub_filt = {
		NULL, 1
	};

	SaEvtEventPatternT pat1 = {"ChanPat1", 8};
	SaEvtEventPatternT pat2 = {"ChanPat2", 8};
	SaEvtEventPatternT pat3 = {"ChanPat3", 8};

	SaEvtEventPatternArrayT evt_pat = {
		NULL, 1
	};


	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtEventHandleT event_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	SaEvtCallbacksT multi_callbacks = {
		0,
		multi_test_callback1
	};

	struct pollfd pfd;
	int nfd;
	int fd;
	int timeout = 5000;


	
	int result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER|SA_EVT_CHANNEL_SUBSCRIBER |
		SA_EVT_CHANNEL_CREATE;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);

	printf("Test multiple operations:\n");

	result = saEvtInitialize (&handle, &multi_callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}
	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		goto evt_fin;
	}

	/*
	 * Allocate an event
	 */
	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate result: %s\n", result_buf);
		goto evt_close;
	}

/*
 * 1. Test multiple subscriptions on a single channel and receiving
 *    events.
 *
 * 		Subscribe twice with two different filters.  Then send three events.
 * 		One will match the first filter, the second will match the second
 * 		filter, the third will match none.  We will validate that we receive
 * 		two events and that the subscription IDs match what we expect for the
 * 		given pattern.
 */

	sub_filt.filters = filt1;
	result = saEvtEventSubscribe(channel_handle,
			&sub_filt,
			sub1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe(1) result: %s\n", result_buf);
		goto evt_free;
	}

	sub_filt.filters = filt2;
	result = saEvtEventSubscribe(channel_handle,
			&sub_filt,
			sub2);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe(2) result: %s\n", result_buf);
		goto evt_free;
	}


	retention_time = 0ULL;

	evt_pat.patterns = &pat1;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result(1): %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventPublish(event_handle, exp_data,  DATA_SIZE, 
								&event_id1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result:(1) %s\n", result_buf);
		goto evt_close;
	}

	evt_pat.patterns = &pat2;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result(2): %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventPublish(event_handle, exp_data,  DATA_SIZE, 
								&event_id2);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result:(2) %s\n", result_buf);
		goto evt_close;
	}

	evt_pat.patterns = &pat3;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result(3): %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventPublish(event_handle, exp_data,  DATA_SIZE, 
								&event_id3);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result:(3) %s\n", result_buf);
		goto evt_close;
	}

	/*
	 * See if we got the event
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtSelectionObject get %s\n", result_buf);
		/* error */
		return;
	}

	while(1) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		nfd = poll(&pfd, 1, timeout);
		if (nfd == 0) {
			break;
		} else if (nfd < 0) {
			perror("ERROR: poll error");
			break;
		}


		result = saEvtDispatch(handle, SA_DISPATCH_ALL);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: saEvtDispatch %s\n", result_buf);
			/* error */
			goto evt_free;
		}
	}


	if (call_count != 2) {
		printf("ERROR: call back count: e=2, a=%d\n", call_count);
		goto evt_free;
	}

	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result: %s\n", result_buf);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
	}
	printf("Done\n");

}

/*
 * Handle call back for multi-test2
 * Counts events received. Makes sure that we get one event from
 * each subscription.
 *
 */
void 
multi_test_callback2(SaEvtSubscriptionIdT my_subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT my_event_data_size)
{
	SaErrorT result;
	SaUint8T my_priority;
	SaTimeT my_retention_time;
	SaNameT my_publisher_name = {0, {0}};
	SaTimeT my_publish_time;
	SaEvtEventIdT my_event_id;
	SaEvtSubscriptionIdT last_sub_id = 0;

	printf("       multi_test_callback2 called(%d)\n", ++call_count);

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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result: %s\n", result_buf);
		goto evt_free;
	}

	if (my_event_id != event_id1) {
		printf("ERROR: Received wrong event\n");
		goto evt_free;
	}

	if (last_sub_id == 0) {
		if (my_subscription_id != sub1 &&
				my_subscription_id != sub2) {
			printf("ERROR: Received bad subscription ID\n");
			goto evt_free;
		}
		last_sub_id = my_subscription_id;
	} else {
		if (my_subscription_id == last_sub_id) {
			printf("ERROR: Received subscription ID twice\n");
			goto evt_free;
		}
		if (my_subscription_id != sub1 &&
				my_subscription_id != sub2) {
			printf("ERROR: Received bad subscription ID\n");
			goto evt_free;
		}
	}

	if (evt_pat_get_array.patternsNumber != 1) {
		printf("ERROR: pattern array count not 1: %d\n", 
					evt_pat_get_array.patternsNumber);
	}

evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}
}

void
test_multi_channel2()
{

	SaEvtEventFilterT filt1[1] = {
		{SA_EVT_EXACT_FILTER, {"ChanPat1", 8}},
	};

	SaEvtEventFilterArrayT sub_filt = {
		NULL, 1
	};

	SaEvtEventPatternT pat1 = {"ChanPat1", 8};

	SaEvtEventPatternArrayT evt_pat = {
		NULL, 1
	};


	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtChannelHandleT channel_handle1;
	SaEvtEventHandleT event_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	SaEvtCallbacksT multi_callbacks = {
		0,
		multi_test_callback2
	};

	struct pollfd pfd;
	int nfd;
	int fd;
	int timeout = 5000;


	
	int result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER|SA_EVT_CHANNEL_SUBSCRIBER |
		SA_EVT_CHANNEL_CREATE;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);

/*
 * 2. Test multiple openings of a single channel and receving events.
 *
 * 		Open and subscribe to a channel twice.  When an event is sent, it
 * 		should be delivered twice, once for each open channel.  
 */
	printf("Test multiple opens/subscribes:\n");

	result = saEvtInitialize (&handle, &multi_callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}

	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open(0) result: %s\n", result_buf);
		goto evt_fin;
	}

	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open(1) result: %s\n", result_buf);
		goto evt_fin;
	}

	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate result: %s\n", result_buf);
		goto evt_close;
	}
	sub_filt.filters = filt1;
	result = saEvtEventSubscribe(channel_handle,
			&sub_filt,
			sub1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe(0) result: %s\n", result_buf);
		goto evt_free;
	}
	sub_filt.filters = filt1;
	result = saEvtEventSubscribe(channel_handle1,
			&sub_filt,
			sub2);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe(1) result: %s\n", result_buf);
		goto evt_free;
	}
	retention_time = 0ULL;

	evt_pat.patterns = &pat1;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result: %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventPublish(event_handle, exp_data,  DATA_SIZE, 
								&event_id1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result: %s\n", result_buf);
		goto evt_close;
	}
	/*
	 * See if we got the event
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtSelectionObject get %s\n", result_buf);
		/* error */
		return;
	}

	call_count = 0;

	while(1) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		nfd = poll(&pfd, 1, timeout);
		if (nfd == 0) {
			break;
		} else if (nfd < 0) {
			perror("ERROR: poll error");
			break;
		}


		result = saEvtDispatch(handle, SA_DISPATCH_ALL);
		if (result != SA_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: saEvtDispatch %s\n", result_buf);
			/* error */
			goto evt_free;
		}
	}


	if (call_count != 2) {
		printf("ERROR: call back count: e=2, a=%d\n", call_count);
		goto evt_free;
	}

	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result(0): %s\n", result_buf);
	}
	result = saEvtChannelClose(channel_handle1);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result(1): %s\n", result_buf);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
	}
	printf("Done\n");

}

/*
 * Handle call back for multi-test3
 * Verifies that the event recevied is associated with the correct 
 * subscription.
 *
 */
void 
multi_test_callback3(SaEvtSubscriptionIdT my_subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT my_event_data_size)
{
	SaErrorT result;
	SaUint8T my_priority;
	SaTimeT my_retention_time;
	SaNameT my_publisher_name = {0, {0}};
	SaTimeT my_publish_time;
	SaEvtEventIdT my_event_id;

	printf("       multi_test_callback2 called(%d)\n", ++call_count);

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
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event get attr result: %s\n", result_buf);
		goto evt_free;
	}

	if ((my_subscription_id != sub1) && (my_subscription_id != sub2)) {
		printf("ERROR: Received wrong subscription ID %lx\n", 
				my_subscription_id);
		printf("       sub1 %lx, sub2 %lx\n", sub1, sub2);
		goto evt_free;
	}

	if ((my_event_id != event_id1) && (my_event_id != event_id2)) {
		printf("ERROR: Received wrong event ID %llx\n", my_event_id);
		printf("       id1 %llx, id2 %llx\n", event_id1, event_id2);
		goto evt_free;
	}

	if ((my_subscription_id == sub1) && (my_event_id != event_id1)) {
		printf("ERROR: Received event on wrong subscription\n");
		goto evt_free;
	}
	if ((my_subscription_id == sub2) && (my_event_id != event_id2)) {
		printf("ERROR: Received event on wrong subscription\n");
		goto evt_free;
	}

	if (evt_pat_get_array.patternsNumber != 1) {
		printf("ERROR: pattern array count not 1: %d\n", 
					evt_pat_get_array.patternsNumber);
	}

evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}
}

void
test_multi_channel3()
{

	SaEvtEventFilterT filt1[1] = {
		{SA_EVT_PREFIX_FILTER, {"ChanPat", 7}},
	};

	SaEvtEventFilterArrayT sub_filt = {
		NULL, 1
	};

	SaEvtEventPatternT pat1 = {"ChanPat1", 8};
	SaEvtEventPatternT pat2 = {"ChanPat2", 8};

	SaEvtEventPatternArrayT evt_pat = {
		NULL, 1
	};


	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtChannelHandleT channel_handle1;
	SaEvtEventHandleT event_handle;
	SaEvtEventHandleT event_handle1;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	SaNameT channel_name1;
	SaEvtCallbacksT multi_callbacks = {
		0,
		multi_test_callback3
	};

	struct pollfd pfd;
	int nfd;
	int fd;
	int timeout = 5000;


	
	int result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER|SA_EVT_CHANNEL_SUBSCRIBER |
		SA_EVT_CHANNEL_CREATE;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel_name.value);
	strcpy(channel_name1.value, channel);
	strcat(channel_name1.value, "_1");
	channel_name1.length = strlen(channel_name1.value);


/*
 * 3. Test opening of multiple channels and receiving events.
 * 		Open and subscribe to two different channels twice.  
 * 		Subscribe to each channel with the same filters.
 * 		Sending an event on one channel should be received in the 
 * 		call-back with the subscription ID corresponding to the sent 
 * 		channel.
 */
	printf("Test multiple different channels/subscribes:\n");

	result = saEvtInitialize (&handle, &multi_callbacks, versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}

	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open(0) result: %s\n", result_buf);
		goto evt_fin;
	}

	result = saEvtChannelOpen(handle, &channel_name1, flags, 0,
				&channel_handle1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open(1) result: %s\n", result_buf);
		goto evt_fin;
	}

	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate(0) result: %s\n", result_buf);
		goto evt_close;
	}
	result = saEvtEventAllocate(channel_handle1, &event_handle1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate(1) result: %s\n", result_buf);
		goto evt_close;
	}

	sub_filt.filters = filt1;
	result = saEvtEventSubscribe(channel_handle,
			&sub_filt,
			sub1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe(0) result: %s\n", result_buf);
		goto evt_free;
	}
	sub_filt.filters = filt1;
	result = saEvtEventSubscribe(channel_handle1,
			&sub_filt,
			sub2);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe(1) result: %s\n", result_buf);
		goto evt_free;
	}
	retention_time = 0ULL;

	evt_pat.patterns = &pat1;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr(0) result: %s\n", result_buf);
		goto evt_free;
	}

	evt_pat.patterns = &pat2;
	result = saEvtEventAttributesSet(event_handle1,
			&evt_pat,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr(1) result: %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventPublish(event_handle, exp_data,  DATA_SIZE, 
								&event_id1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result: %s\n", result_buf);
		goto evt_close;
	}
	result = saEvtEventPublish(event_handle1, exp_data,  DATA_SIZE, 
								&event_id2);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result: %s\n", result_buf);
		goto evt_close;
	}
	/*
	 * See if we got the events
	 */
	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtSelectionObject get %s\n", result_buf);
		/* error */
		return;
	}

	call_count = 0;

	while(1) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		nfd = poll(&pfd, 1, timeout);
		if (nfd == 0) {
			break;
		} else if (nfd < 0) {
			perror("ERROR: poll error");
			break;
		}


		result = saEvtDispatch(handle, SA_DISPATCH_ALL);
		if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
			printf("ERROR: saEvtDispatch %s\n", result_buf);
			/* error */
			goto evt_free;
		}
	}


	if (call_count != 2) {
		printf("ERROR: call back count: e=2, a=%d\n", call_count);
		goto evt_free;
	}

	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}
	result = saEvtEventFree(event_handle1);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result(0): %s\n", result_buf);
	}
	result = saEvtChannelClose(channel_handle1);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result(1): %s\n", result_buf);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
	}
	printf("Done\n");

}

/*
 * Test event retention
 *  	Test 1: publish the event with a retention time and then 
 *  			subscribe.  If the event was retained, we should receive it.
 *
 * 		Test 2: Publish the event, sleep until it expires, then 
 * 				subscribe.  We shouldn't get an event delivered.
 *
 * 		Test 3: Publish an event with a retention time.
 * 				subscribe.
 * 				wait for it.
 * 				unsubscribe
 * 				Clear it.
 * 				Then subscribe and make sure that the messages isn't delivered.
 *
 */
#define EXPIRE_TIME 10 /* Seconds */
SaEvtEventIdT retained_id;
int got_event;

void 
event_callback_retained(SaEvtSubscriptionIdT my_subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT my_event_data_size)
{
	SaErrorT result;
	//printf("event_callback_retained called\n");
	result = saEvtEventAttributesGet(event_handle,
			0,		/* patterns */
			0,		/* priority */
			0,		/* retention time */
			0,		/* publisher name */
			0,		/* publish time */
			&retained_id	/* event_id */
			);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: callback attr get result: %s\n", result_buf);
		return;
	}
	got_event = 1;
}

void
test_retention()
{
	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtEventHandleT event_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;
	SaEvtCallbacksT callbacks_retain = {
		0,
		event_callback_retained
	};

	struct pollfd pfd;
	int nfd;
	int fd;
	int timeout = (EXPIRE_TIME + 5);
	SaErrorT result;
	 
	flags = SA_EVT_CHANNEL_PUBLISHER|SA_EVT_CHANNEL_SUBSCRIBER;
	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);

	printf("Test Event retention:\n");

	result = saEvtInitialize (&handle, &callbacks_retain, 
			versions[0].version);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Initialize result: %s\n", result_buf);
		return;
	}

	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtSelectionObject get %s\n", result_buf);
		/* error */
		return;
	}

	result = saEvtChannelOpen(handle, &channel_name, flags, 0,
				&channel_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel open result: %s\n", result_buf);
		goto evt_fin;
	}

	/*
	 * Allocate an event
	 */
	result = saEvtEventAllocate(channel_handle, &event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Allocate result: %s\n", result_buf);
		goto evt_close;
	}


	retention_time = (EXPIRE_TIME)*1000000000ULL;
	result = saEvtEventAttributesSet(event_handle,
			&evt_pat_set_array,
			TEST_PRIORITY,
			retention_time,
			&test_pub_name);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event set attr result: %s\n", result_buf);
		goto evt_free;
	}

	/*
	 * Test 1: publish the event with a retention time and then 
	 * subscribe.  If the event was retained, we should receive it.
	 */
	printf("       Receive retained event\n");
	got_event=0;
	retained_id=0;
	result = saEvtEventPublish(event_handle, exp_data,  0, &event_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result(1): %s\n", result_buf);
		goto evt_close;
	}


	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		goto evt_free;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	nfd = poll(&pfd, 1, 1000);
	if (nfd <= 0) {
		printf("ERROR: poll fds %d\n", nfd);
		if (nfd < 0) {
			perror("ERROR: poll error");
		}
		/* Error */
		goto evt_free;
	}

	result = saEvtDispatch(handle, SA_DISPATCH_ONE);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtDispatch %s\n", result_buf);
		/* error */
		goto evt_free;
	}

	if (!got_event) {
		printf("ERROR: retained event not recevied\n");
		goto evt_free;
	}
	if (retained_id != event_id) {
		printf("ERROR: received the wrong event: e=%llx, a=%llx\n",
			event_id, retained_id);
		goto evt_free;
	}

	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: unsubscribe result: %s\n", result_buf);
		goto evt_free;
	}

	/*
	 * Test 2:  Publish the event, sleep until it expires, then 
	 * subscribe.  We shouldn't get an event delivered.
	 */
	printf("       Expire retained event\n");
	got_event=0;
	retained_id=0;
	result = saEvtEventPublish(event_handle, exp_data,  0, &event_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result(1): %s\n", result_buf);
		goto evt_close;
	}

	result = saEvtSelectionObjectGet(handle, &fd);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtSelectionObject get %s\n", result_buf);
		/* error */
		return;
	}

	/*
	 * Wait for the event to expire, then subscribe.  We shouldn't get 
	 * an event
	 */
	sleep(timeout);

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		result = saEvtChannelClose(channel_handle);
		goto evt_free;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	nfd = poll(&pfd, 1, 1000);
	if (nfd != 0) {
		printf("ERROR: poll fds %d\n", nfd);
		if (nfd < 0) {
			perror("ERROR: poll error");
		}
		/* Error */
		goto evt_free;
	}

	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: unsubscribe result: %s\n", result_buf);
		goto evt_free;
	}

	/*
	 * Test 3:
	 * Publish an event with a retention time.
	 * subscribe.
	 * wait for it.
	 * unsubscribe
	 * Clear it.
	 * Then subscribe and make sure that the messages isn't delivered.
	 */
	printf("       Clear event retention time\n");
	got_event=0;
	retained_id=0;
	result = saEvtEventPublish(event_handle, exp_data,  0, &event_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event Publish result(2): %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		goto evt_free;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	nfd = poll(&pfd, 1, 1000);
	if (nfd <= 0) {
		printf("ERROR: poll fds %d\n", nfd);
		if (nfd < 0) {
			perror("ERROR: poll error");
		}
		/* Error */
		goto evt_free;
	}

	result = saEvtDispatch(handle, SA_DISPATCH_ONE);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: saEvtDispatch %s\n", result_buf);
		/* error */
		goto evt_free;
	}

	if (!got_event) {
		printf("ERROR: retained event not recevied\n");
		goto evt_free;
	}
	if (retained_id != event_id) {
		printf("ERROR: received the wrong event: e=%llx, a=%llx\n",
			event_id, retained_id);
		goto evt_free;
	}

	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: unsubscribe result: %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventRetentionTimeClear(channel_handle, event_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: clear retention time result: %s\n", result_buf);
		goto evt_free;
	}

	result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters,
			subscription_id);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event subscribe result: %s\n", result_buf);
		goto evt_free;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	nfd = poll(&pfd, 1, 1000);
	if (nfd != 0) {
		printf("ERROR: poll fds %d\n", nfd);
		if (nfd < 0) {
			perror("ERROR: poll error");
		}
		/* Error */
		goto evt_free;
	}

	/*
	 * Test cleanup
	 */
evt_free:
	result = saEvtEventFree(event_handle);
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: event free result: %s\n", result_buf);
	}

evt_close:
	result = saEvtChannelClose(channel_handle);
	
	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel close result: %s\n", result_buf);
	}
evt_fin:
	result = saEvtFinalize(handle);

	if (result != SA_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
	}
	printf("Done\n");

}
int main (void)
{
	test_initialize ();
	test_channel();
	test_event();
	test_multi_channel1();
	test_multi_channel2();
	test_multi_channel3();
	test_retention();

	return (0);
}
