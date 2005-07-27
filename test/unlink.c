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

#define TRY_WAIT 2

extern int get_sa_error(SaAisErrorT, char *, int);
char result_buf[256];
int result_buf_len = sizeof(result_buf);

SaVersionT version = { 'B', 0x01, 0x01 };

SaEvtCallbacksT callbacks = {
	0,
	0
};


char channel[256] = "EVENT_TEST_CHANNEL";

	
int
do_unlink()
{
	SaEvtHandleT handle;
	SaNameT channel_name;

	SaAisErrorT result;
	 
	do {
		result = saEvtInitialize (&handle, &callbacks, &version);
	} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("Event Initialize result: %s\n", result_buf);
		return(result);
	}

	strcpy(channel_name.value, channel);
	channel_name.length = strlen(channel);
	do {
	result = saEvtChannelUnlink(handle, &channel_name);
	} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: channel unlink result: %s\n", result_buf);
	}

	do {
		result = saEvtFinalize(handle);
	} while ((result == SA_AIS_ERR_TRY_AGAIN) && !sleep(TRY_WAIT));
	if (result != SA_AIS_OK) {
		get_sa_error(result, result_buf, result_buf_len);
		printf("ERROR: Event Finalize result: %s\n", result_buf);
	}
	return 0;
}

int main (int argc, char **argv)
{
	static const char opts[] = "c:";

	int option;

	while (1) {
		option = getopt(argc, argv, opts);
		if (option == -1) 
			break;

		switch (option) {

		case 'c':
			strcpy(channel, optarg);
			break;
		default:
			printf("invalid arg: \"%s\"\n", optarg);
			return 1;
		}
	}
	do_unlink();

	return 0;
}
