/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
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

#include <config.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <grp.h>
#include <pwd.h>
#include <netdb.h>
#include <nl_types.h>
#include <libgen.h>
#include <dlfcn.h>
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif /* HAVE_UTMPX_H */
#include <search.h>
#include <locale.h>
#include <dirent.h>
#include <pthread.h>

#include <tsafe.h>

#ifndef RTLD_NEXT
#define RTLD_NEXT       ((void *) -1l)
#endif /* !RTLD_NEXT */

#ifdef COROSYNC_BSD
#define CONST_ON_BSD const
#else
#define CONST_ON_BSD
#endif /* COROSYNC_BSD */



/*
 * The point of this file is to prevent people from using functions that are
 * known not to be thread safe (see man pthreads).
 */

static int tsafe_disabled = 1;
static int tsafe_inited = 0;
static char **coro_environ;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
static pthread_spinlock_t tsafe_enabled_mutex;
#else
static pthread_mutex_t tsafe_enabled_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif


void tsafe_init (char **envp)
{
	int i;
	int size = 1;

	for (i = 0; envp[i] != NULL; i++) {
		size++;
	}
	coro_environ = malloc (sizeof (char*) * size);
	for (i = 0; envp[i] != NULL; i++) {
		coro_environ[i] = strdup (envp[i]);
	}
	coro_environ[size-1] = NULL;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spin_init (&tsafe_enabled_mutex, 0);
#endif
	tsafe_disabled = 1;
	tsafe_inited = 1;
}


static void tsafe_lock (void)
{
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spin_lock (&tsafe_enabled_mutex);
#else
	pthread_mutex_lock (&tsafe_enabled_mutex);
#endif
}
static void tsafe_unlock (void)
{
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spin_unlock (&tsafe_enabled_mutex);
#else
	pthread_mutex_unlock (&tsafe_enabled_mutex);
#endif
}

void tsafe_off (void)
{
	tsafe_lock ();
	tsafe_disabled = 1;
	tsafe_unlock ();
}

void tsafe_on (void)
{
	tsafe_lock ();
	tsafe_disabled = 0;
	tsafe_unlock ();
}


static void* _get_real_func_(const char * func_name)
{
	void * func = NULL;
#ifdef COROSYNC_BSD
	static void *handle;
	/*
	 * On BSD we open the libc and retrive the pointer to "func_name".
	 */
	handle = dlopen("/usr/lib/libc.so", RTLD_LAZY);
	func = dlsym (handle, func_name);
#else
	/*
	 * On linux/Sun we can set func to next instance of "func_name",
	 * which is the original "func_name"
	 */
	func = dlsym (RTLD_NEXT, func_name);
#endif
	return func;
}

#ifdef COROSYNC_LINUX
/* we are implementing these functions only to know when to turn tsafe
 * on and off.
 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg)
{
	static int (*real_pthread_create)(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg) = NULL;

	if (tsafe_inited && tsafe_disabled) {
		tsafe_on ();
	}

	if (real_pthread_create == NULL) {
		real_pthread_create = _get_real_func_ ("pthread_create");
	}
	return real_pthread_create (thread, attr, start_routine, arg);
}

pid_t fork(void)
{
	static pid_t (*real_fork)(void) = NULL;
	pid_t ret;

	if (tsafe_inited && !tsafe_disabled) {
		tsafe_off ();
	}

	if (real_fork == NULL) {
		real_fork = _get_real_func_ ("fork");
	}
	ret = real_fork ();
	if (ret == 0) {
		/* if we have forked corosync then anything goes. */
		tsafe_off ();
	}
	return ret;
}
#endif /* COROSYNC_LINUX */

/*
 * we will allow this one as there doesn't seem to be any other option.
 * and only used in lcr
 * char *dlerror(void)
 * {
 * }
 */

/* the following functions are needed so we are either banning them or
 * re-implementing them (depending on tsafe_on).
 */
char *getenv(const char *name)
{
	static char* (*real_getenv)(const char *name) = NULL;
	int entry;
	int found = 0;
	char *eq;
	size_t name_len;

	if (!tsafe_inited || tsafe_disabled) {
		if (real_getenv == NULL) {
			real_getenv = _get_real_func_ ("getenv");
		}
		return real_getenv (name);
	}
	for (entry = 0; coro_environ[entry] != NULL; entry++) {
		eq = strchr (coro_environ[entry], '=');
		name_len = eq - coro_environ[entry];

		if (name_len == strlen (name) &&
			strncmp(name, coro_environ[entry], name_len) == 0) {
			found = 1;
			break;
		}
	}

	if (found) {
		eq = strchr (coro_environ[entry], '=');
		return (eq + 1);
	}
	return NULL;
}

/* all the following functions are banned and will result in an abort.
 */

int setenv(const char *name, const char *value, int overwrite)
{
	static int (*real_setenv)(const char *name, const char *value, int overwrite) = NULL;

	if (!tsafe_inited || tsafe_disabled) {
		if (real_setenv == NULL) {
			real_setenv = _get_real_func_ ("setenv");
		}
		return real_setenv (name, value, overwrite);
	}
	assert (0);
	return 0;
}

int unsetenv(const char *name)
{
	static int (*real_unsetenv)(const char *name) = NULL;

	if (!tsafe_inited || tsafe_disabled) {
		if (real_unsetenv == NULL) {
			real_unsetenv = _get_real_func_ ("unsetenv");
		}
		return real_unsetenv (name);
	}
	assert (0);
	return 0;
}

int putenv(char *string)
{
	static int (*real_putenv)(char *string) = NULL;

	if (!tsafe_inited || tsafe_disabled) {
		if (real_putenv == NULL) {
			real_putenv = _get_real_func_ ("putenv");
		}
		return real_putenv (string);
	}
	assert (0);
	return 0;
}

char *asctime(const struct tm *tm)
{
	static char * (*real_asctime)(const struct tm *tm) = NULL;

	if (!tsafe_inited || tsafe_disabled) {
		if (real_asctime == NULL) {
			real_asctime = _get_real_func_ ("asctime");
		}
		return real_asctime (tm);
	}
	assert(0);
	return NULL;
}

char *basename (CONST_ON_BSD char *path)
{
	static char *(*real_basename)(CONST_ON_BSD char *path) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_basename == NULL) {
			real_basename = _get_real_func_ ("basename");
		}
		return real_basename (path);
	}
	assert(0);
	return NULL;
}

char *catgets(nl_catd catalog, int set_number,
	int message_number, const char *message)
{
	return NULL;
}

#ifdef _XOPEN_SOURCE
char *crypt(const char *key, const char *salt)
{
	static char *(*real_crypt)(const char *key, const char *salt) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_crypt == NULL) {
			real_crypt = _get_real_func_ ("crypt");
		}
		return real_crypt (salt);
	}
	assert(0);
	return NULL;
}
#endif /* _XOPEN_SOURCE */

char *ctermid(char *s)
{
	static char *(*real_ctermid)(char *s) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_ctermid == NULL) {
			real_ctermid = _get_real_func_ ("ctermid");
		}
		return real_ctermid (s);
	}
	assert(0);
	return NULL;
}

char *ctime(const time_t *timep)
{
	static char *(*real_ctime)(const time_t *timep) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_ctime == NULL) {
			real_ctime = _get_real_func_ ("ctime");
		}
		return real_ctime (timep);
	}
	assert(0);
	return NULL;
}

char *dirname(CONST_ON_BSD char *path)
{
	static char *(*real_dirname)(CONST_ON_BSD char *path) = NULL;

	if (!tsafe_inited || tsafe_disabled) {
		if (real_dirname == NULL) {
			real_dirname = _get_real_func_ ("dirname");
		}
		return real_dirname (path);
	}
	assert(0);
	return NULL;
}

double drand48(void)
{
	static double (*real_drand48)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_drand48 == NULL) {
			real_drand48 = _get_real_func_ ("drand48");
		}
		return real_drand48 ();
	}
	assert(0);
	return 0;
}

#ifdef _XOPEN_SOURCE
void encrypt(char block[64], int edflag)
{
	static void (*real_encrypt)(char block[64], int edflag) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_encrypt == NULL) {
			real_encrypt = _get_real_func_ ("encrypt");
		}
		return real_encrypt (edflag);
	}
	assert(0);
}
#endif /* _XOPEN_SOURCE */

void endgrent(void)
{
	static void (*real_endgrent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_endgrent == NULL) {
			real_endgrent = _get_real_func_ ("endgrent");
		}
		return real_endgrent ();
	}
	assert(0);
}

void endpwent(void)
{
	static void (*real_endpwent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_endpwent == NULL) {
			real_endpwent = _get_real_func_ ("endpwent");
		}
		return real_endpwent ();
	}
	assert(0);
}

#ifdef _XOPEN_SOURCE
struct tm *getdate(const char *string)
{
	static struct tm *(*real_getdate)(const char *string) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getdate == NULL) {
			real_getdate = _get_real_func_ ("getdate");
		}
		return real_getdate (string);
	}
	assert(0);
	return NULL;
}
#endif /* _XOPEN_SOURCE */

struct group *getgrent(void)
{
	static struct group *(*real_getgrent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getgrent == NULL) {
			real_getgrent = _get_real_func_ ("getgrent");
		}
		return real_getgrent ();
	}
	assert(0);
	return NULL;
}

struct group *getgrgid(gid_t gid)
{
	static struct group *(*real_getgrgid)(gid_t gid) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getgrgid == NULL) {
			real_getgrgid = _get_real_func_ ("getgrgid");
		}
		return real_getgrgid (gid);
	}
	assert(0);
	return NULL;
}

struct group *getgrnam(const char *name)
{
	static struct group *(*real_getgrnam)(const char *name) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getgrnam == NULL) {
			real_getgrnam = _get_real_func_ ("getgrnam");
		}
		return real_getgrnam (name);
	}
	assert(0);
	return NULL;
}

struct hostent *gethostent(void)
{
	static struct hostent *(*real_gethostent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_gethostent == NULL) {
			real_gethostent = _get_real_func_ ("gethostent");
		}
		return real_gethostent ();
	}
	assert(0);
	return NULL;
}

char *getlogin(void)
{
	static char *(*real_getlogin)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getlogin == NULL) {
			real_getlogin = _get_real_func_ ("getlogin");
		}
		return real_getlogin ();
	}
	assert(0);
	return NULL;
}

struct netent *getnetbyaddr(uint32_t net, int type)
{
	static struct netent *(*real_getnetbyaddr)(uint32_t net, int type) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getnetbyaddr == NULL) {
			real_getnetbyaddr = _get_real_func_ ("getnetbyaddr");
		}
		return real_getnetbyaddr (net, type);
	}
	assert(0);
	return NULL;
}

struct netent *getnetbyname(const char *name)
{
	static struct netent *(*real_getnetbyname)(const char *name) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getnetbyname == NULL) {
			real_getnetbyname = _get_real_func_ ("getnetbyname");
		}
		return real_getnetbyname (name);
	}
	assert(0);
	return NULL;
}

struct netent *getnetent(void)
{
	static struct netent *(*real_getnetent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getnetent == NULL) {
			real_getnetent = _get_real_func_ ("getnetent");
		}
		return real_getnetent ();
	}
	assert(0);
	return NULL;
}

struct protoent *getprotobyname(const char *name)
{
	static struct protoent *(*real_getprotobyname)(const char *name) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getprotobyname == NULL) {
			real_getprotobyname = _get_real_func_ ("getprotobyname");
		}
		return real_getprotobyname (name);
	}
	assert(0);
	return NULL;
}

struct protoent *getprotobynumber(int proto)
{
	static struct protoent *(*real_getprotobynumber)(int proto) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getprotobynumber == NULL) {
			real_getprotobynumber = _get_real_func_ ("getprotobynumber");
		}
		return real_getprotobynumber (proto);
	}
	assert(0);
	return NULL;
}

struct protoent *getprotoent(void)
{
	static struct protoent *(*real_getprotoent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getprotoent == NULL) {
			real_getprotoent = _get_real_func_ ("getprotoent");
		}
		return real_getprotoent ();
	}
	assert(0);
	return NULL;
}

struct passwd *getpwent(void)
{
	static struct passwd *(*real_getpwent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getpwent == NULL) {
			real_getpwent = _get_real_func_ ("getpwent");
		}
		return real_getpwent ();
	}
	assert(0);
	return NULL;
}

struct passwd *getpwnam(const char *name)
{
	static struct passwd *(*real_getpwnam)(const char *name) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getpwnam == NULL) {
			real_getpwnam = _get_real_func_ ("getpwnam");
		}
		return real_getpwnam (name);
	}
	assert(0);
	return NULL;
}

struct passwd *getpwuid(uid_t uid)
{
	static struct passwd *(*real_getpwuid)(uid_t uid) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getpwuid == NULL) {
			real_getpwuid = _get_real_func_ ("getpwuid");
		}
		return real_getpwuid (uid);
	}
	assert(0);
	return NULL;
}

struct servent *getservent(void)
{
	static struct servent *(*real_getservent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getservent == NULL) {
			real_getservent = _get_real_func_ ("getservent");
		}
		return real_getservent ();
	}
	assert(0);
	return NULL;
}

struct servent *getservbyname(const char *name, const char *proto)
{
	static struct servent *(*real_getservbyname)(const char *name, const char *proto) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getservbyname == NULL) {
			real_getservbyname = _get_real_func_ ("getservbyname");
		}
		return real_getservbyname (name, proto);
	}
	assert(0);
	return NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
	static struct servent *(*real_getservbyport)(int port, const char *proto) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getservbyport == NULL) {
			real_getservbyport = _get_real_func_ ("getservbyport");
		}
		return real_getservbyport (port, proto);
	}
	assert(0);
	return NULL;
}

#ifdef HAVE_UTMPX_H
struct utmpx *getutxent(void)
{
	static struct utmpx *(*real_getutxent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getutxent == NULL) {
			real_getutxent = _get_real_func_ ("getutxent");
		}
		return real_getutxent ();
	}
	assert(0);
	return NULL;
}

struct utmpx *getutxid(const struct utmpx *a)
{
	static struct utmpx *(*real_getutxid)(const struct utmpx *a) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getutxid == NULL) {
			real_getutxid = _get_real_func_ ("getutxid");
		}
		return real_getutxid (a);
	}
	assert(0);
	return NULL;
}

struct utmpx *getutxline(const struct utmpx *a)
{
	static struct utmpx *(*real_getutxline)(const struct utmpx *a) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_getutxline == NULL) {
			real_getutxline = _get_real_func_ ("getutxline");
		}
		return real_getutxline (a);
	}
	assert(0);
	return NULL;
}
#endif /* HAVE_UTMPX_H */

struct tm *gmtime(const time_t *timep)
{
	static struct tm *(*real_gmtime)(const time_t *timep) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_gmtime == NULL) {
			real_gmtime = _get_real_func_ ("gmtime");
		}
		return real_gmtime (timep);
	}
	assert(0);
	return NULL;
}

int hcreate(size_t nel)
{
	static int (*real_hcreate)(size_t nel) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_hcreate == NULL) {
			real_hcreate = _get_real_func_ ("hcreate");
		}
		return real_hcreate (nel);
	}
	assert(0);
	return 0;
}

ENTRY *hsearch(ENTRY item, ACTION action)
{
	static ENTRY *(*real_hsearch)(ENTRY item, ACTION action) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_hsearch == NULL) {
			real_hsearch = _get_real_func_ ("hsearch");
		}
		return real_hsearch (item, action);
	}
	assert(0);
	return NULL;
}

void hdestroy(void)
{
	static void (*real_hdestroy)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_hdestroy == NULL) {
			real_hdestroy = _get_real_func_ ("hdestroy");
		}
		return real_hdestroy ();
	}
	assert(0);
}

char *inet_ntoa(struct in_addr in)
{
	static char *(*real_inet_ntoa)(struct in_addr in) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_inet_ntoa == NULL) {
			real_inet_ntoa = _get_real_func_ ("inet_ntoa");
		}
		return real_inet_ntoa (in);
	}
	assert(0);
	return NULL;
}

char *l64a(long value)
{
	static char *(*real_l64a)(long value) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_l64a == NULL) {
			real_l64a = _get_real_func_ ("l64a");
		}
		return real_l64a (value);
	}
	assert(0);
	return NULL;
}

double lgamma(double x)
{
	static double (*real_lgamma)(double x) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_lgamma == NULL) {
			real_lgamma = _get_real_func_ ("lgamma");
		}
		return real_lgamma (x);
	}
	assert(0);
	return 0;

}

float lgammaf(float x)
{
	static float (*real_lgammaf)(float x) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_lgammaf == NULL) {
			real_lgammaf = _get_real_func_ ("lgammaf");
		}
		return real_lgammaf (x);
	}
	assert(0);
	return 0;
}

#ifdef COROSYNC_LINUX
long double lgammal(long double x)
{
	static long double (*real_lgammal)(long double x) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_lgammal == NULL) {
			real_lgammal = _get_real_func_ ("lgammal");
		}
		return real_lgammal (x);
	}
	assert(0);
	return 0;
}
#endif

struct lconv *localeconv(void)
{
	static struct lconv *(*real_localeconv)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_localeconv == NULL) {
			real_localeconv = _get_real_func_ ("localeconv");
		}
		return real_localeconv ();
	}
	assert(0);
	return NULL;
}

struct tm *localtime(const time_t *timep)
{
	static struct tm *(*real_localtime)(const time_t *timep) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_localtime == NULL) {
			real_localtime = _get_real_func_ ("localtime");
		}
		return real_localtime (timep);
	}
	assert(0);
	return NULL;
}

long int lrand48(void)
{
	static long int (*real_lrand48)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_lrand48 == NULL) {
			real_lrand48 = _get_real_func_ ("lrand48");
		}
		return real_lrand48 ();
	}
	assert(0);
	return 0;
}

long int mrand48(void)
{
	static long int (*real_mrand48)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_mrand48 == NULL) {
			real_mrand48 = _get_real_func_ ("mrand48");
		}
		return real_mrand48 ();
	}
	assert(0);
	return 0;
}

#ifdef COROSYNC_LINUX
struct utmpx *pututxline(const struct utmpx *a)
{
	static struct utmpx *(*real_pututxline)(const struct utmpx *a) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_pututxline == NULL) {
			real_pututxline = _get_real_func_ ("pututxline");
		}
		return real_pututxline (a);
	}
	assert(0);
	return NULL;
}
#endif

int rand(void)
{
	static int (*real_rand)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_rand == NULL) {
			real_rand = _get_real_func_ ("rand");
		}
		return real_rand ();
	}
	assert(0);
	return 0;
}


struct dirent *readdir(DIR *dirp)
{
	static struct dirent *(*real_readdir)(DIR *dirp) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_readdir == NULL) {
			real_readdir = _get_real_func_ ("readdir");
		}
		return real_readdir (dirp);
	}
	assert(0);
	return NULL;
}


#ifdef COROSYNC_BSD
int setgrent(void)
{
	static int (*real_setgrent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_setgrent == NULL) {
			real_setgrent = _get_real_func_ ("setgrent");
		}
		return real_setgrent ();
	}
	assert(0);
	return 0;
}
#else
void setgrent(void)
{
	static void (*real_setgrent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_setgrent == NULL) {
			real_setgrent = _get_real_func_ ("setgrent");
		}
		real_setgrent ();
	}
	assert(0);
}
#endif

#ifdef _XOPEN_SOURCE
void setkey(const char *key)
{
	static void (*real_setkey)(const char *key) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_setkey == NULL) {
			real_setkey = _get_real_func_ ("setkey");
		}
		return real_setkey (key);
	}
	assert(0);
}
#endif /* _XOPEN_SOURCE */

void setpwent(void)
{
	static void (*real_setpwent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_setpwent == NULL) {
			real_setpwent = _get_real_func_ ("setpwent");
		}
		return real_setpwent ();
	}
	assert(0);
}

void setutxent(void)
{
	static void (*real_setutxent)(void) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_setutxent == NULL) {
			real_setutxent = _get_real_func_ ("setutxent");
		}
		return real_setutxent ();
	}
	assert(0);
}

char *strerror(int errnum)
{
	static char *(*real_strerror)(int errnum) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_strerror == NULL) {
			real_strerror = _get_real_func_ ("strerror");
		}
		return real_strerror (errnum);
	}
	assert(0);
	return NULL;
}

char *strsignal(int sig)
{
	static char *(*real_strsignal)(int sig) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_strsignal == NULL) {
			real_strsignal = _get_real_func_ ("strsignal");
		}
		return real_strsignal (sig);
	}
	assert(0);
	return NULL;
}

char *strtok(char *str, const char *delim)
{
	static char *(*real_strtok)(char *str, const char *delim) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_strtok == NULL) {
			real_strtok = _get_real_func_ ("strtok");
		}
		return real_strtok (str, delim);
	}
	assert(0);
	return NULL;
}

int system(const char *command)
{
	static int (*real_system)(const char *command) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_system == NULL) {
			real_system = _get_real_func_ ("system");
		}
		return real_system (command);
	}
	assert(0);
	return 0;
}

char *tmpnam(char *s)
{
	static char *(*real_tmpnam)(char *s) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_tmpnam == NULL) {
			real_tmpnam = _get_real_func_ ("tmpnam");
		}
		return real_tmpnam (s);
	}
	assert(0);
	return NULL;
}

char *ttyname(int fd)
{
	static char *(*real_ttyname)(int fd) = NULL;
	if (!tsafe_inited || tsafe_disabled) {
		if (real_ttyname == NULL) {
			real_ttyname = _get_real_func_ ("ttyname");
		}
		return real_ttyname (fd);
	}
	assert(0);
	return NULL;
}


