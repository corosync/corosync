#include <string.h>
#include <errno.h>
#include "ais_types.h"

const char *sa_error_list[] = {
	"OUT_OF_RANGE",
	"SA_OK",
	"SA_ERR_LIBRARY",
	"SA_ERR_VERSION",
	"SA_ERR_INIT",
	"SA_ERR_TIMEOUT",
	"SA_ERR_TRY_AGAIN",
	"SA_ERR_INVALID_PARAM",
	"SA_ERR_NO_MEMORY",
	"SA_ERR_BAD_HANDLE",
	"SA_ERR_BUSY",
	"SA_ERR_ACCESS",
	"SA_ERR_NOT_EXIST",
	"SA_ERR_NAME_TOO_LONG",
	"SA_ERR_EXIST",
	"SA_ERR_NO_SPACE",
	"SA_ERR_INTERRUPT",
	"SA_ERR_SYSTEM",
	"SA_ERR_NAME_NOT_FOUND",
	"SA_ERR_NO_RESOURCES",
	"SA_ERR_NOT_SUPPORTED",
	"SA_ERR_BAD_OPERATION",
	"SA_ERR_FAILED_OPERATION",
	"SA_ERR_MESSAGE_ERROR",
	"SA_ERR_NO_MESSAGE",
	"SA_ERR_QUEUE_FULL",
	"SA_ERR_QUEUE_NOT_AVAILABLE",
	"SA_ERR_BAD_CHECKPOINT",
	"SA_ERR_BAD_FLAGS",
	"SA_ERR_SECURITY",
};

int get_sa_error(SaErrorT error, char *str, int len)
{
	if (error < SA_OK || 
			error > SA_ERR_SECURITY || 
					len < strlen(sa_error_list[error])) {
			errno = EINVAL;
		return -1;
	}
	strncpy(str, sa_error_list[error], len);
	return 0;
}
