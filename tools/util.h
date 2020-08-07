#ifndef COROSYNC_TOOLS_UTIL_H_DEFINED
#define COROSYNC_TOOLS_UTIL_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

extern int              util_strtonum(const char *str, long long int min_val,
    long long int max_val, long long int *res);

#ifdef __cplusplus
}
#endif

#endif /* COROSYNC_TOOLS_UTIL_H_DEFINED */
