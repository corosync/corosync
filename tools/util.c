#include <stdlib.h>
#include <errno.h>

#include "util.h"

/*
 * Safer wrapper of strtoll. Return 0 on success, otherwise -1.
 * Idea from corosync-qdevice project
 */
int
util_strtonum(const char *str, long long int min_val, long long int max_val,
    long long int *res)
{
        long long int tmp_ll;
        char *ep;

        if (min_val > max_val) {
                return (-1);
        }

        errno = 0;

        tmp_ll = strtoll(str, &ep, 10);
        if (ep == str || *ep != '\0' || errno != 0) {
                return (-1);
        }

        if (tmp_ll < min_val || tmp_ll > max_val) {
                return (-1);
        }

        *res = tmp_ll;

        return (0);
}
