/*
 * Test program for event service subscriptions
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <getopt.h>
#include "saAis.h"
#include "saEvt.h"

#define  TEST_EVENT_ORDER 1
#define  EVT_FREQ 1000
#define  TRY_WAIT 2
uint32_t evt_count = 0;

extern int get_sa_error(SaAisErrorT, char *, int);
char result_buf[256];
int result_buf_len = sizeof(result_buf);

int quiet = 0;

SaVersionT version = { 'B', 0x01, 0x01 };

void event_callback( SaEvtSubscriptionIdT subscriptionId,
		const SaEvtEventHandleT eventHandle,
		const SaSizeT eventDataSize);

SaEvtCallbacksT callbacks = {
	0,
	event_callback
};

char channel[256] = "EVENT_TEST_CHANNEL";

#define MAX_NODES 256
SaEvtEventIdT last_event_id[MAX_NODES] = {0,};

#define MAX_SUB 100

uint32_t subscription_id[MAX_SUB] = {0xfedcba98};

int sub_next = 0;

char pubname[256] = "Test Pub Name";
	
#define patt1 "Filter pattern 1"
#define patt1_size sizeof(patt1)

SaEvtEventFilterT filters[MAX_SUB] = {
	{SA_EVT_PASS_ALL_FILTER, {0, 0}}
};

SaEvtEventFilterArrayT subscribe_filters[MAX_SUB] = {
	{ 
		1, &filters[0] 
	},
};


#define PAT_SIZE 100
SaUint8T pat0[PAT_SIZE];
SaUint8T pat1[PAT_SIZE];
SaUint8T pat2[PAT_SIZE];
SaUint8T pat3[PAT_SIZE];
SaUint8T pat4[PAT_SIZE];
SaEvtEventPatternT evt_patts[5] = {
	{PAT_SIZE, PAT_SIZE, pat0},
	{PAT_SIZE, PAT_SIZE, pat1},
	{PAT_SIZE, PAT_SIZE, pat2},
	{PAT_SIZE, PAT_SIZE, pat3},
	{PAT_SIZE, PAT_SIZE, pat4}};
SaEvtEventPatternArrayT	evt_pat_get_array = { 5, 0, evt_patts };

SaNameT test_pub_name = {13, "Test Pub Name"};


char user_data_file[256];
char  user_data[65536];
char  event_data[65536];
int user_data_size = 0;

int
test_subscription()
{
	SaEvtHandleT handle;
	SaEvtChannelHandleT channel_handle;
	SaEvtChannelOpenFlagsT flags;
	SaNameT channel_name;

	struct pollfd pfd;
	int nfd;
	SaSelectionObjectT fd;
	int timeout = 60000;
	int i;


	
	SaAisErrorT result;
	 
	flags = SA_EVT_CHANNEL_SUBSCRIBER | SA_EVT_CHANNEL_CREATE;
	strcpy((char *)channel_name.value, channel);
	channel_name.length = strlen(channel);

	printf("Test subscription:\n");

	do {
		result = saEvtInitialize (&handle, &callbacks, &version);
	} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("Event Initialize result: %s\n", result_buf);
		return result;
	}
	do {
		result = saEvtChannelOpen(handle, &channel_name, flags, 
				SA_TIME_MAX, &channel_handle);
	} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("channel open result: %s\n", result_buf);
		goto init_fin;
	}

	if (sub_next == 0) 
		sub_next = 1;

	for (i = 0; i < sub_next; i++) {
		do {
			result = saEvtEventSubscribe(channel_handle,
			&subscribe_filters[i],
			subscription_id[i]);
		} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));

		if (result != SA_AIS_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("event subscribe result: %s\n", result_buf);
			goto chan_fin;
		}

	}
	/*
	 * See if we got the event
	 */
	do {
		result = saEvtSelectionObjectGet(handle, &fd);
	} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("saEvtSelectionObject get %s\n", result_buf);
		goto sub_fin;
	}

	while (1) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		nfd = poll(&pfd, 1, timeout);
		if (nfd < 0) {
			printf("poll fds %d\n", nfd);
			perror("poll error");
			goto sub_fin;
		} else if (nfd == 0) {
			printf("Still waiting\n");
			continue;
		}

		if (pfd.revents & (POLLERR|POLLHUP)) {
			printf("Error recieved on poll fd %lld\n", fd);
			result =  SA_AIS_ERR_BAD_OPERATION;
			goto sub_fin;
		}
		do {
			result = saEvtDispatch(handle, SA_DISPATCH_ONE);
		} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
		if (result != SA_AIS_OK) {
			get_sa_error(result, result_buf, result_buf_len);
			printf("saEvtDispatch %s\n", result_buf);
			goto sub_fin;
		}
		if (!quiet)
			printf(" - - - - - - - - - - - - - - - -\n\n");
	}

sub_fin:
#if 0
	result = saEvtEventUnsubscribe(channel_handle, subscription_id);
	if (result != SA_AIS_OK) 
		printf("Channel unsubscribe result: %d\n", result);
#endif
chan_fin:
	result = saEvtChannelClose(channel_handle);
	if (result != SA_AIS_OK) 
		get_sa_error(result, result_buf, result_buf_len);
		printf("Channel close result: %s\n", result_buf);
init_fin:
	result = saEvtFinalize(handle);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("Finalize result: %s\n", result_buf);
	}

	return result;
}

static char time_buf[1024];

char *ais_time_str(SaTimeT time)
{
	time_t t;
	t = time / 1000000000ULL;
	strcpy(time_buf, ctime(&t));
	return time_buf;
}

#define dprintf(format, ...) 			\
	{					\
	if (did_dot) {				\
		printf("\n");			\
	}					\
	printf(format, ## __VA_ARGS__);		\
	did_dot = 0;				\
	}

void 
event_callback( SaEvtSubscriptionIdT subscription_id,
		const SaEvtEventHandleT event_handle,
		const SaSizeT event_data_size)
{
	static int did_dot = 0;
	SaAisErrorT result;
	SaUint8T priority;
	SaTimeT retention_time;
	SaNameT publisher_name = {0, {0}};
	SaTimeT publish_time;
	SaEvtEventIdT event_id;
	SaSizeT received_size;
	int i;
#ifdef TEST_EVENT_ORDER
	int idx;
#endif

	if (!quiet)
		dprintf("event_callback called\n");
	if (!quiet)
		dprintf("sub ID: %x\n", subscription_id);
	if (!quiet)
		dprintf("event_handle %llx\n", (unsigned long long)event_handle);
	if (!quiet)
		dprintf("event data size %lld\n", event_data_size);

	evt_pat_get_array.patterns[0].patternSize = PAT_SIZE;
	evt_pat_get_array.patterns[1].patternSize = PAT_SIZE;
	evt_pat_get_array.patterns[2].patternSize = PAT_SIZE;
	evt_pat_get_array.patterns[3].patternSize = PAT_SIZE;
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
		dprintf("event get attr result(2): %s\n", result_buf);
		goto evt_free;
	}
	if (!quiet) {
		dprintf("pattern array count: %lld\n", 
				evt_pat_get_array.patternsNumber);
		for (i = 0; i < evt_pat_get_array.patternsNumber; i++) {
			dprintf( "pattern %d =\"%s\"\n", i,
				  evt_pat_get_array.patterns[i].pattern);
		}

		dprintf("priority: 0x%x\n", priority);
		dprintf("retention: 0x%llx\n", (unsigned long long)retention_time);
		dprintf("publisher name content: \"%s\"\n", 
				publisher_name.value); 
	}

	if (event_id == SA_EVT_EVENTID_LOST) {
		dprintf("*** Events have been dropped at %s",
			ais_time_str(publish_time));

		if ((evt_pat_get_array.patternsNumber == 0)|| 
			(strcmp((char *)evt_pat_get_array.patterns[0].pattern, SA_EVT_LOST_EVENT) != 0)) {
			dprintf("*** Received SA_EVT_EVENTID_LOST but pattern is wrong: %s\n",
				evt_pat_get_array.patterns[0].pattern);
		}
	}
	if (quiet < 2) {
		dprintf("event id: 0x%016llx\n", (unsigned long long)event_id);
	}
	if (quiet == 2) {
		if ((++evt_count % EVT_FREQ) == 0) {
			fprintf(stderr, ".");
			did_dot = 1;
		}
	}

	if (event_id == SA_EVT_EVENTID_LOST) {
		goto evt_free;
	}

#ifdef TEST_EVENT_ORDER
	for (idx = 0; idx < MAX_NODES; idx++) {
		if (last_event_id[idx] == 0) {
			last_event_id[idx] = event_id;
			break;
		} else {
			if ((last_event_id[idx] >> 32) == (event_id >> 32)) {
				last_event_id[idx]++;
				if (last_event_id[idx] != event_id) {
					dprintf("*** expected %016llx got %016llx event_id\n",
					(unsigned long long)last_event_id[idx],
					(unsigned long long)event_id);
				last_event_id[idx] = event_id;
				}
				break;
			}
		}
	}
	if (idx == MAX_NODES) {
			dprintf("*** Too many nodes in cluster\n");
			exit(1);
	}
#endif

	if (event_data_size != user_data_size) {
		dprintf("unexpected data size: e=%d, a=%lld\n",
				user_data_size, event_data_size);
		goto evt_free;
	} 

	received_size = user_data_size;
	result = saEvtEventDataGet(event_handle, event_data,
			&received_size);
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		dprintf("event get data result: %s\n", result_buf);
		goto evt_free;
	}
	if (received_size != event_data_size) {
		dprintf("event data mismatch e=%lld, a=%lld\n",
				event_data_size, 
				received_size);
		goto evt_free;
	}
	if (memcmp(user_data, event_data, user_data_size) != 0 ) {
		dprintf("event data doesn't match specified file data\n");
		goto evt_free;
	}
	if (!quiet) {
		dprintf("Received %d bytes of data OK\n", 
				user_data_size);
	}

evt_free:
	result = saEvtEventFree(event_handle);
	if (!quiet) {
		get_sa_error(result, result_buf, result_buf_len);
		dprintf("event free result: %s\n", result_buf);
	}
}

static int err_wait_time = -1;

int main (int argc, char **argv)
{
	static const char opts[] = "c:s:n:qu:f:";

	int option;
	char *p;

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
		case 'q':
			quiet++;
			break;
		case 'c':
			strcpy(channel, optarg);
			break;
		case 'f':
			err_wait_time = 
				(unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'n':
			strcpy(pubname, optarg);
			break;
		case 's':
			p = strsep(&optarg, ",");
			subscription_id[sub_next] = 
				(unsigned int)strtoul(p, NULL, 0);
			p = strsep(&optarg, ",");
			filters[sub_next].filter.pattern = malloc(strlen(p));
			strcpy((char *)filters[sub_next].filter.pattern, p);
			filters[sub_next].filter.patternSize = strlen(p);
			p = strsep(&optarg, ",");
			filters[sub_next++].filterType = strtoul(p,0, 0);
			break;
		default:
			printf("invalid arg: \"%s\"\n", optarg);
			return 1;
		}
	}
	do {
		if (test_subscription() != SA_AIS_OK) {
			if (err_wait_time > 0) {
				sleep(err_wait_time);
			} else {
				return 1;
			}
		}
	} while (err_wait_time > 0);

	return 0;
}

