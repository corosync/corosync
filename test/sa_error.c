#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "ais_types.h"

const char *sa_error_list[] = {
	"OUT_OF_RANGE",
	"SA_AIS_OK",
	"SA_AIS_ERR_LIBRARY",
	"SA_AIS_ERR_VERSION",
	"SA_AIS_ERR_INIT",
	"SA_AIS_ERR_TIMEOUT",
	"SA_AIS_ERR_TRY_AGAIN",
	"SA_AIS_ERR_INVALID_PARAM",
	"SA_AIS_ERR_NO_MEMORY",
	"SA_AIS_ERR_BAD_HANDLE",
	"SA_AIS_ERR_BUSY",
	"SA_AIS_ERR_ACCESS",
	"SA_AIS_ERR_NOT_EXIST",
	"SA_AIS_ERR_NAME_TOO_LONG",
	"SA_AIS_ERR_EXIST",
	"SA_AIS_ERR_NO_SPACE",
	"SA_AIS_ERR_INTERRUPT",
	"SA_AIS_ERR_NAME_NOT_FOUND",
	"SA_AIS_ERR_NO_RESOURCES",
	"SA_AIS_ERR_NOT_SUPPORTED",
	"SA_AIS_ERR_BAD_OPERATION",
	"SA_AIS_ERR_FAILED_OPERATION",
	"SA_AIS_ERR_MESSAGE_ERROR",
	"SA_AIS_ERR_QUEUE_FULL",
	"SA_AIS_ERR_QUEUE_NOT_AVAILABLE",
	"SA_AIS_ERR_BAD_CHECKPOINT",
	"SA_AIS_ERR_BAD_FLAGS",
	"SA_AIS_ERR_NO_SECTIONS",
};

int get_sa_error(SaErrorT error, char *str, int len)
{
	if (error < SA_AIS_OK || 
			error > SA_AIS_ERR_NO_SECTIONS || 
					len < strlen(sa_error_list[error])) {
			errno = EINVAL;
		return -1;
	}
	strncpy(str, sa_error_list[error], len);
	return 0;
}

char *get_sa_error_b (SaAisErrorT error) {
	return (sa_error_list[error]);
}

char *get_test_output (SaAisErrorT result, SaAisErrorT expected) {
        static *test_result[256];

        if (result == expected) {
                return ("PASSED");
        } else {
                sprintf (test_result,
                        "FAILED expected %s got %s",
			get_sa_error_b(expected), get_sa_error_b(result));
                return (test_result);
        }
}
