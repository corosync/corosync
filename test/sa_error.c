#include <config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "saAis.h"

const char *sa_error_list[] = {
	"OUT_OF_RANGE",
	"CS_OK",
	"CS_ERR_LIBRARY",
	"CS_ERR_VERSION",
	"CS_ERR_INIT",
	"CS_ERR_TIMEOUT",
	"CS_ERR_TRY_AGAIN",
	"CS_ERR_INVALID_PARAM",
	"CS_ERR_NO_MEMORY",
	"CS_ERR_BAD_HANDLE",
	"CS_ERR_BUSY",
	"CS_ERR_ACCESS",
	"CS_ERR_NOT_EXIST",
	"CS_ERR_NAME_TOO_LONG",
	"CS_ERR_EXIST",
	"CS_ERR_NO_SPACE",
	"CS_ERR_INTERRUPT",
	"CS_ERR_NAME_NOT_FOUND",
	"CS_ERR_NO_RESOURCES",
	"CS_ERR_NOT_SUPPORTED",
	"CS_ERR_BAD_OPERATION",
	"CS_ERR_FAILED_OPERATION",
	"CS_ERR_MESSAGE_ERROR",
	"CS_ERR_QUEUE_FULL",
	"CS_ERR_QUEUE_NOT_AVAILABLE",
	"CS_ERR_BAD_CHECKPOINT",
	"CS_ERR_BAD_FLAGS",
	"CS_ERR_NO_SECTIONS",
};

int get_sa_error(cs_error_t error, char *str, int len)
{
	if (error < CS_OK ||
			error > CS_ERR_NO_SECTIONS ||
					len < strlen(sa_error_list[error])) {
			errno = EINVAL;
		return -1;
	}
	strncpy(str, sa_error_list[error], len);
	if (len > 0) {
		str[len - 1] = '\0';
	}
	return 0;
}

char *get_sa_error_b (cs_error_t error) {
	return ((char *)sa_error_list[error]);
}

char *get_test_output (cs_error_t result, cs_error_t expected) {
static char test_result[256];

        if (result == expected) {
                return ("PASSED");
        } else {
                snprintf (test_result, sizeof(test_result),
                        "FAILED expected %s got %s",
			get_sa_error_b(expected), get_sa_error_b(result));
                return (test_result);
        }
}
