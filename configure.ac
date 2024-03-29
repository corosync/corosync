#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# bootstrap / init
AC_PREREQ([2.69])

AC_INIT([corosync],
	[m4_esyscmd([build-aux/git-version-gen .tarball-version .gitarchivever])],
	[users@clusterlabs.org])

AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign 1.11])

LT_PREREQ([2.2.6])
LT_INIT

AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([lib/cpg.c])
AC_CONFIG_HEADERS([include/corosync/config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST

AC_LANG([C])

AC_SUBST(WITH_LIST, [""])

#Enable inter-library dependencies
AC_ARG_ENABLE(interlib-deps,
    [AS_HELP_STRING([--disable-interlib-deps  ],[disable inter-library dependencies (might break builds)])],
    [enable_interlib_deps="$enableval"],
    [enable_interlib_deps="yes"])

AC_MSG_NOTICE([enable inter-library dependencies: $enable_interlib_deps])
if test "x${enable_interlib_deps}" = "xyes"; then
    link_all_deplibs=yes
    link_all_deplibs_CXX=yes
else
    link_all_deplibs=no
    link_all_deplibs_CXX=no
fi

AC_ARG_ENABLE([rust-bindings],
	[AS_HELP_STRING([--enable-rust-bindings],[rust bindings support])],,
	[ enable_rust_bindings="no" ])
AM_CONDITIONAL([BUILD_RUST_BINDINGS], [test x$enable_rust_bindings = xyes])
corosyncrustver=["`echo ${VERSION} | sed 's/\.\([^-\.]*-\)/-\1/'`"]
AC_SUBST([corosyncrustver])

dnl Fix default variables - "prefix" variable if not specified
systemddir=${prefix}/lib/systemd/system

if test "$prefix" = "NONE"; then
	prefix="/usr"

	dnl Fix "localstatedir" variable if not specified
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	dnl Fix "sysconfdir" variable if not specified
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	if test "$systemddir" = "NONE/lib/systemd/system"; then
		systemddir=/lib/systemd/system
	fi
	dnl Fix "libdir" variable if not specified
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

AC_MSG_NOTICE(Sanitizing exec_prefix: ${exec_prefix})
case $exec_prefix in
  dnl For consistency with Corosync, map NONE->$prefix
  NONE)   exec_prefix=$prefix;;
  prefix) exec_prefix=$prefix;;
esac

# Checks for programs.

# check stolen from gnulib/m4/gnu-make.m4
if ! ${MAKE-make} --version /cannot/make/this >/dev/null 2>&1; then
	AC_MSG_ERROR([you don't seem to have GNU make; it is required])
fi

sinclude(corosync-default.m4)

AC_PROG_CC
m4_version_prereq([2.70], [:], [AC_PROG_CC_C99])
if test "x$ac_cv_prog_cc_c99" = "xno"; then
	AC_MSG_ERROR(["C99 support is required"])
fi
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_SED
PKG_PROG_PKG_CONFIG

AC_CHECK_PROGS([GROFF], [groff])
AC_CHECK_PROGS([AUGTOOL], [augtool])
AC_CHECK_PROGS([DOT], [dot])
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AC_CHECK_PROGS([AWK], [awk])
AC_PATH_PROG([BASHPATH], [bash])

# Checks for compiler characteristics.
AC_PROG_GCC_TRADITIONAL
AC_C_CONST
AC_C_INLINE
AC_C_VOLATILE

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h \
		  stdlib.h string.h sys/ioctl.h sys/param.h sys/socket.h \
		  sys/time.h syslog.h unistd.h sys/types.h getopt.h malloc.h \
		  utmpx.h ifaddrs.h stddef.h sys/file.h sys/uio.h])

# Check entries in specific structs
AC_CHECK_MEMBER([struct sockaddr_in.sin_len],
		[AC_DEFINE_UNQUOTED([HAVE_SOCK_SIN_LEN], [1], [sockaddr_in needs sin_len])],
		[], [[#include <netinet/in.h>]])
AC_CHECK_MEMBER([struct sockaddr_in6.sin6_len],
		[AC_DEFINE_UNQUOTED([HAVE_SOCK_SIN6_LEN], [1], [sockaddr_in6 needs sin6_len])],
		[], [[#include <netinet/in.h>]])
AC_CHECK_MEMBER([struct msghdr.msg_control],
		[AC_DEFINE_UNQUOTED([HAVE_MSGHDR_CONTROL], [1], [msghdr has msg_control])],
		[], [[#include <sys/socket.h>]])
AC_CHECK_MEMBER([struct msghdr.msg_controllen],
		[AC_DEFINE_UNQUOTED([HAVE_MSGHDR_CONTROLLEN], [1], [msghdr has msg_controllen])],
		[], [[#include <sys/socket.h>]])
AC_CHECK_MEMBER([struct msghdr.msg_flags],
		[AC_DEFINE_UNQUOTED([HAVE_MSGHDR_FLAGS], [1], [msghdr has msg_flags])],
		[], [[#include <sys/socket.h>]])
AC_CHECK_MEMBER([struct msghdr.msg_accrights],
		[AC_DEFINE_UNQUOTED([HAVE_MSGHDR_ACCRIGHTS], [1], [msghdr has msg_accrights])],
		[], [[#include <sys/socket.h>]])
AC_CHECK_MEMBER([struct msghdr.msg_accrightslen],
		[AC_DEFINE_UNQUOTED([HAVE_MSGHDR_ACCRIGHTSLEN], [1], [msghdr has msg_accrightslen])],
		[], [[#include <sys/socket.h>]])

# Checks for typedefs.
AC_TYPE_UID_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for libraries.
SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LIBS="$LIBS"
PKG_CHECK_MODULES([LIBQB], [libqb])
CPPFLAGS="$CPPFLAGS $LIBQB_CFLAGS"
LIBS="$LIBS $LIBQB_LIBS"
AC_CHECK_LIB([qb], [qb_log_thread_priority_set], \
	     have_qb_log_thread_priority_set="yes", \
	     have_qb_log_thread_priority_set="no")
if test "x${have_qb_log_thread_priority_set}" = xyes; then
	AC_DEFINE_UNQUOTED([HAVE_QB_LOG_THREAD_PRIORITY_SET], 1, [have qb_log_thread_priority_set])
fi
AC_CHECK_LIB([qb], [qb_log_file_reopen], \
	     have_qb_log_file_reopen="yes", \
	     have_qb_log_file_reopen="no")
if test "x${have_qb_log_file_reopen}" = xyes; then
	AC_DEFINE_UNQUOTED([HAVE_QB_LOG_FILE_REOPEN], 1, [have qb_log_file_reopen])
fi
AM_CONDITIONAL(HAVE_QB_LOG_FILE_REOPEN, test x$have_qb_log_file_reopen = xyes)

CPPFLAGS="$SAVE_CPPFLAGS"
LIBS="$SAVE_LIBS"
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([socket], [socket])
PKG_CHECK_MODULES([knet],[libknet])
AC_CHECK_LIB([nsl], [t_open])
AC_CHECK_LIB([rt], [sched_getscheduler])
AC_CHECK_LIB([z], [crc32],
    AM_CONDITIONAL([HAVE_CRC32], true),
    AM_CONDITIONAL([HAVE_CRC32], false))

# this hack is necessary to check for symbols on out of tree builds
# but it is as horrible as it gets and in theory users should be
# invoking ./configure with proper LIBRARY_PATH set.
OLDLIBS="$LIBS"
LIBS="$knet_LIBS $LIBS"
AC_CHECK_LIB([knet],[knet_handle_enable_access_lists],
	     [AC_DEFINE_UNQUOTED([HAVE_KNET_ACCESS_LIST], 1, [have knet access list])])
AC_CHECK_LIB([knet],[knet_handle_crypto_set_config],
	     [AC_DEFINE_UNQUOTED([HAVE_KNET_CRYPTO_RECONF], 1, [have knet crypto reconfig support])])
AC_CHECK_LIB([knet],[knet_handle_get_onwire_ver],
	     [AC_DEFINE_UNQUOTED([HAVE_KNET_ONWIRE_VER], 1, [have knet onwire versioning])])
LIBS="$OLDLIBS"

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_VPRINTF

AC_CHECK_FUNCS([alarm alphasort atexit bzero dup2 endgrent endpwent fdatasync \
		fcntl getcwd getpeerucred getpeereid gettimeofday inet_ntoa \
		memmove memset mkdir scandir select socket strcasecmp strchr \
		strdup strerror strrchr strspn strstr pthread_setschedparam \
		sched_get_priority_max sched_setscheduler getifaddrs \
		clock_gettime ftruncate gethostname localtime_r munmap strtol])

AC_CONFIG_FILES([Makefile
		 exec/Makefile
		 include/Makefile
		 init/Makefile
		 lib/Makefile
		 common_lib/Makefile
		 man/Makefile
		 pkgconfig/Makefile
		 test/Makefile
		 tools/Makefile
		 conf/Makefile
		 vqsim/Makefile
		 Doxyfile
		 conf/logrotate/Makefile
		 bindings/Makefile
		 bindings/rust/Makefile
		 bindings/rust/tests/Makefile
		 bindings/rust/Cargo.toml
		 bindings/rust/tests/Cargo.toml])

### Local business

# check for rust tools to build bindings
if test "x$enable_rust_bindings" = "xyes"; then
	AC_PATH_PROG([CARGO], [cargo], [no])
	if test "x$CARGO" = xno; then
		AC_MSG_ERROR(["cargo command not found"])
	fi

	AC_PATH_PROG([RUSTC], [rustc], [no])
	if test "x$RUSTC" = xno; then
		AC_MSG_ERROR(["rustc command not found"])
	fi

	AC_PATH_PROG([RUSTDOC], [rustdoc], [no])
	if test "x$RUSTDOC" = xno; then
		AC_MSG_ERROR(["rustdoc command not found"])
	fi

	AC_PATH_PROG([BINDGEN], [bindgen], [no])
	if test "x$BINDGEN" = xno; then
		AC_MSG_ERROR(["bindgen command not found"])
	fi

	AC_PATH_PROG([CLIPPY], [clippy-driver], [no])
	if test "x$CLIPPY" = xno; then
		AC_MSG_ERROR(["clippy-driver command not found"])
	fi

	AC_PATH_PROG([RUSTFMT], [rustfmt], [no])
	if test "x$RUSTFMT" = xno; then
		AC_MSG_ERROR(["rustfmt command not found"])
	fi
fi

dnl ===============================================
dnl Functions / global M4 variables
dnl ===============================================
dnl Global list of LIB names
m4_define([local_soname_list], [])dnl

dnl Upcase parameter
m4_define([local_upcase], [translit([$*], [a-z], [A-Z])])dnl

dnl M4 macro for include lib/lib$1.soname and subst that
m4_define([LIB_SONAME_IMPORT],[dnl
m4_define([local_libname], local_upcase($1)[_SONAME])dnl
m4_define([local_soname], translit(m4_sinclude(lib/lib$1.verso), [
], []))dnl
local_libname="local_soname"dnl
m4_define([local_soname_list], m4_defn([local_soname_list])[,]local_libname[,]local_upcase($1))dnl
AC_SUBST(local_libname)dnl
])dnl

dnl M4 macro for print padspaces (used in LIB_MSG_RESULT). It takes 2 arguments, length of string to pad and desired
dnl (padded) length
m4_define([m4_printpadspace],[ifelse(m4_eval([$2 - $1 < 1]),[1],,[ ][m4_printpadspace([$1],m4_eval([$2 - 1]))])])dnl

dnl Show AC_MSG_RESULT for specific libraries
m4_define([LIB_MSG_RESULT], [ifelse([$#], [1],  ,[dnl
AC_MSG_RESULT([  $2 Library SONAME m4_printpadspace(len($2),8) = ${$1}])
LIB_MSG_RESULT(m4_shift(m4_shift($@)))dnl
])])dnl

# ===============================================
# Helpers
# ===============================================

## check if the compiler supports -Werror -Wunknown-warning-option
AC_MSG_CHECKING([whether $CC supports -Wunknown-warning-option -Werror])
BACKUP="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS -Werror -Wunknown-warning-option"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
		  [unknown_warnings_as_errors='-Wunknown-warning-option -Werror'; AC_MSG_RESULT([yes])],
		  [unknown_warnings_as_errors=''; AC_MSG_RESULT([no])])
CPPFLAGS="$BACKUP"

## helper for CC stuff
cc_supports_flag() {
	BACKUP="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $@ $unknown_warnings_as_errors"
	AC_MSG_CHECKING([whether $CC supports "$@"])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
			  [RC=0; AC_MSG_RESULT([yes])],
			  [RC=1; AC_MSG_RESULT([no])])
	CPPFLAGS="$BACKUP"
	return $RC
}

## local defines
PACKAGE_FEATURES=""

LINT_FLAGS="-weak -unrecog +posixlib +ignoresigns -fcnuse \
	-badflag -D__gnuc_va_list=va_list -D__attribute\(x\)="

# default libraries SONAME
SOMAJOR="5"
SOMINOR="0"
SOMICRO="0"
SONAME="${SOMAJOR}.${SOMINOR}.${SOMICRO}"

# specific libraries SONAME
LIB_SONAME_IMPORT([cfg])
LIB_SONAME_IMPORT([cpg])
LIB_SONAME_IMPORT([quorum])
LIB_SONAME_IMPORT([sam])
LIB_SONAME_IMPORT([votequorum])
LIB_SONAME_IMPORT([cmap])

# local options
AC_ARG_ENABLE([ansi],
	[  --enable-ansi                   : force to build with ANSI standards. ],
	[ default="no" ])

AC_ARG_ENABLE([fatal-warnings],
	[  --enable-fatal-warnings         : enable fatal warnings. ],
	[ default="no" ])

AC_ARG_ENABLE([debug],
	[  --enable-debug                  : enable debug build. ],
	[ default="no" ])

AC_ARG_WITH([sanitizers],
	[AS_HELP_STRING([--with-sanitizers=...,...],
			[enable SANitizer build, do *NOT* use for production. Only ASAN/UBSAN/TSAN are currently supported])],
	[ SANITIZERS="$withval" ],
	[ SANITIZERS="" ])

AC_ARG_ENABLE([secure-build],
	[  --enable-secure-build           : enable PIE/RELRO build. ],
	[],
	[enable_secure_build="yes"])

AC_ARG_ENABLE([user-flags],
	[  --enable-user-flags             : rely on user environment. ],
	[ default="no" ])

AC_ARG_ENABLE([coverage],
	[  --enable-coverage               : coverage analysis of the codebase. ],
	[ default="no" ])

AC_ARG_ENABLE([small-memory-footprint],
	[  --enable-small-memory-footprint : Use small message queues and small messages sizes. ],
	[ default="no" ])

AC_ARG_ENABLE([dbus],
	[  --enable-dbus                   : dbus events. ],,
	[ enable_dbus="no" ])

AC_ARG_ENABLE([monitoring],
	[  --enable-monitoring             : resource monitoring ],,
	[ default="no" ])
AM_CONDITIONAL(BUILD_MONITORING, test x$enable_monitoring = xyes)

AC_ARG_ENABLE([watchdog],
	[  --enable-watchdog               : Watchdog support ],,
	[ default="no" ])
AM_CONDITIONAL(BUILD_WATCHDOG, test x$enable_watchdog = xyes)

AC_ARG_ENABLE([augeas],
	[  --enable-augeas                 : Install the augeas lens for corosync.conf ],,
	[ enable_augeas="no" ])
AM_CONDITIONAL(INSTALL_AUGEAS, test x$enable_augeas = xyes)

AC_ARG_ENABLE([systemd],
	      [  --enable-systemd                : Install systemd service files],,
	[ enable_systemd="no" ])
AM_CONDITIONAL(INSTALL_SYSTEMD, test x$enable_systemd = xyes)

AC_ARG_WITH([initconfigdir],
	[AS_HELP_STRING([--with-initconfigdir=DIR],
		[configuration directory @<:@SYSCONFDIR/sysconfig@:>@])],
	[INITCONFIGDIR="$withval"],
	[INITCONFIGDIR='${sysconfdir}/sysconfig'])
AC_SUBST([INITCONFIGDIR])

AC_ARG_WITH([initddir],
	[  --with-initddir=DIR     : path to init script directory. ],
	[ INITDDIR="$withval" ],
	[ INITDDIR="$sysconfdir/init.d" ])

AC_ARG_WITH([systemddir],
	[  --with-systemddir=DIR   : path to systemd unit files directory. ],
	[ SYSTEMDDIR="$withval" ],
	[ SYSTEMDDIR="$systemddir" ])

AC_ARG_WITH([logdir],
	[  --with-logdir=DIR       : the base directory for corosync logging files. ],
	[ LOGDIR="$withval" ],
	[ LOGDIR="$localstatedir/log/cluster" ])

AC_ARG_WITH([logrotatedir],
	[  --with-logrotatedir=DIR : the base directory for logrorate.d files.  ],
	[ LOGROTATEDIR="$withval" ],
	[ LOGROTATEDIR="$sysconfdir/logrotate.d" ])

AC_ARG_ENABLE([snmp],
	[  --enable-snmp                   : SNMP protocol support ],
	[ default="no" ])

AC_ARG_ENABLE([xmlconf],
	[  --enable-xmlconf                : XML configuration support ],,
	[ enable_xmlconf="no" ])
AM_CONDITIONAL(INSTALL_XMLCONF, test x$enable_xmlconf = xyes)

AC_ARG_ENABLE([vqsim],
	[  --enable-vqsim                  : Quorum simulator support ],,
	[ enable_vqsim="no" ])
AM_CONDITIONAL(BUILD_VQSIM, test x$enable_vqsim = xyes)

AC_ARG_ENABLE([nozzle],
	[  --enable-nozzle                 : Support for nozzle ],,
	[ enable_nozzle="no" ])

# *FLAGS handling goes here

ENV_CFLAGS="$CFLAGS"
ENV_CPPFLAGS="$CPPFLAGS"
ENV_LDFLAGS="$LDFLAGS"

# debug build stuff
if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Compiling Debugging code])
	OPT_CFLAGS="-O0"
	PACKAGE_FEATURES="$PACKAGE_FEATURES debug"
	RUST_FLAGS=""
	RUST_TARGET_DIR="debug"
else
	OPT_CFLAGS="-O3"
	RUST_FLAGS="--release"
	RUST_TARGET_DIR="release"
fi

# gdb flags
if test "x${GCC}" = xyes; then
	GDB_FLAGS="-ggdb3"
else
	GDB_FLAGS="-g"
fi

# --- ASAN/UBSAN/TSAN (see man gcc) ---
# when using SANitizers, we need to pass the -fsanitize..
# to both CFLAGS and LDFLAGS. The CFLAGS/LDFLAGS must be
# specified as first in the list or there will be runtime
# issues (for example user has to LD_PRELOAD asan for it to work
# properly).

if test -n "${SANITIZERS}"; then
	SANITIZERS=$(echo $SANITIZERS | sed -e 's/,/ /g')
	for SANITIZER in $SANITIZERS; do
		case $SANITIZER in
			asan|ASAN)
				SANITIZERS_CFLAGS="$SANITIZERS_CFLAGS -fsanitize=address"
				SANITIZERS_LDFLAGS="$SANITIZERS_LDFLAGS -fsanitize=address -lasan"
				AC_CHECK_LIB([asan],[main],,AC_MSG_ERROR([Unable to find libasan]))
				;;
			ubsan|UBSAN)
				SANITIZERS_CFLAGS="$SANITIZERS_CFLAGS -fsanitize=undefined"
				SANITIZERS_LDFLAGS="$SANITIZERS_LDFLAGS -fsanitize=undefined -lubsan"
				AC_CHECK_LIB([ubsan],[main],,AC_MSG_ERROR([Unable to find libubsan]))
				;;
			tsan|TSAN)
				SANITIZERS_CFLAGS="$SANITIZERS_CFLAGS -fsanitize=thread"
				SANITIZERS_LDFLAGS="$SANITIZERS_LDFLAGS -fsanitize=thread -ltsan"
				AC_CHECK_LIB([tsan],[main],,AC_MSG_ERROR([Unable to find libtsan]))
				;;
		esac
	done
fi

# Look for dbus-1
if test "x${enable_dbus}" = xyes; then
	PKG_CHECK_MODULES([DBUS],[dbus-1])
	AC_DEFINE_UNQUOTED([HAVE_DBUS], 1, [have dbus])
	PACKAGE_FEATURES="$PACKAGE_FEATURES dbus"
	WITH_LIST="$WITH_LIST --with dbus"
fi

if test "x${enable_monitoring}" = xyes; then
	PKG_CHECK_MODULES([statgrab], [libstatgrab])
	PKG_CHECK_MODULES([statgrabge090], [libstatgrab >= 0.90],
	    AC_DEFINE_UNQUOTED([HAVE_LIBSTATGRAB_GE_090], 1, [have libstatgrab >= 0.90]),
	    TMP_VARIABLE=1)
	AC_DEFINE_UNQUOTED([HAVE_MONITORING], 1, [have resource monitoring])
	PACKAGE_FEATURES="$PACKAGE_FEATURES monitoring"
	WITH_LIST="$WITH_LIST --with monitoring"
fi

if test "x${enable_watchdog}" = xyes; then
	AC_CHECK_HEADER([linux/watchdog.h], [], [AC_MSG_ERROR([watchdog requires linux/watchdog.h])])
	AC_CHECK_HEADER([linux/reboot.h], [], [AC_MSG_ERROR([watchdog requires linux/reboot.h])])
	AC_DEFINE_UNQUOTED([HAVE_WATCHDOG], 1, [have watchdog])
	PACKAGE_FEATURES="$PACKAGE_FEATURES watchdog"
	WITH_LIST="$WITH_LIST --with watchdog"
fi

if test "x${enable_augeas}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES augeas"
fi
if test "x${enable_systemd}" = xyes; then
	PKG_CHECK_MODULES([libsystemd], [libsystemd])
	AC_DEFINE([HAVE_LIBSYSTEMD], [1], [have systemd interface library])
	PACKAGE_FEATURES="$PACKAGE_FEATURES systemd"
	WITH_LIST="$WITH_LIST --with systemd"
fi
if test "x${enable_xmlconf}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES xmlconf"
	WITH_LIST="$WITH_LIST --with xmlconf"
fi
if test "x${enable_vqsim}" = xyes; then
	vqsim_readline=no
	AC_CHECK_HEADERS([readline/readline.h readline/history.h],
			 [],
                         AC_MSG_WARN([vqsim will lack readline support]))
	PACKAGE_FEATURES="$PACKAGE_FEATURES vqsim"
	WITH_LIST="$WITH_LIST --with vqsim"
fi
AM_CONDITIONAL(VQSIM_READLINE, [test "x${ac_cv_header_readline_readline_h}" = xyes])

# Look for nozzle
if test "x${enable_nozzle}" = xyes; then
	PKG_CHECK_MODULES([nozzle],[libnozzle])
	AC_DEFINE_UNQUOTED([HAVE_LIBNOZZLE], 1, [have nozzle])
	PACKAGE_FEATURES="$PACKAGE_FEATURES nozzle"
	WITH_LIST="$WITH_LIST --with nozzle"
fi

do_snmp=0
if test "x${enable_snmp}" = xyes; then
	AC_PATH_PROGS([SNMPCONFIG], [net-snmp-config])

	if test "x${SNMPCONFIG}" != "x"; then
		AC_MSG_CHECKING([for snmp includes])
		SNMP_PREFIX=`$SNMPCONFIG --prefix`
		SNMP_INCLUDES="-I$SNMP_PREFIX/include"
		AC_MSG_RESULT([$SNMP_INCLUDES])

		AC_MSG_CHECKING([for snmp libraries])
		SNMP_LIBS=`$SNMPCONFIG --libs`
		AC_MSG_RESULT([$SNMP_LIBS])
		AC_SUBST([SNMP_LIBS])

		saveCFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $SNMP_INCLUDES"
		AC_CHECK_HEADERS([net-snmp/net-snmp-config.h])
		CFLAGS="$saveCFLAGS"

		if test "x${ac_cv_header_net_snmp_net_snmp_config_h}" != "xyes"; then
			AC_MSG_ERROR([Unable to use net-snmp/net-snmp-config.h])
		fi

		savedLibs=$LIBS
		LIBS="$LIBS $SNMP_LIBS"
		AC_CHECK_FUNCS([netsnmp_transport_open_client])
		if test $ac_cv_func_netsnmp_transport_open_client != yes; then
			AC_CHECK_FUNCS([netsnmp_tdomain_transport])
			if test $ac_cv_func_netsnmp_tdomain_transport != yes; then
				AC_MSG_ERROR([No usable SNMP client transport implementation found])
			fi
		else
			AC_DEFINE_UNQUOTED([NETSNMPV54], $NETSNMP_NEW_SUPPORT, [have net-snmp5.4 over])
		fi
		LIBS=$savedLibs

		do_snmp=1
		PACKAGE_FEATURES="$PACKAGE_FEATURES snmp"
		WITH_LIST="$WITH_LIST --with snmp"
		AC_DEFINE_UNQUOTED([ENABLE_SNMP], $do_snmp, [Build in support for sending SNMP traps])
	else
		AC_MSG_ERROR([You need the net_snmp development package to continue.])
	fi
fi
AM_CONDITIONAL(BUILD_SNMP, test "${do_snmp}" = "1")

# extra warnings
EXTRA_WARNINGS=""

WARNLIST="
	all
	shadow
	missing-prototypes
	missing-declarations
	strict-prototypes
	pointer-arith
	write-strings
	cast-align
	bad-function-cast
	missing-format-attribute
	format=2
	format-security
	format-nonliteral
	no-long-long
	unsigned-char
	no-strict-aliasing
	"

for j in $WARNLIST; do
	if cc_supports_flag -W$j; then
		EXTRA_WARNINGS="$EXTRA_WARNINGS -W$j";
	fi
done

if test "x${enable_coverage}" = xyes && \
		cc_supports_flag -ftest-coverage && \
		cc_supports_flag -fprofile-arcs ; then
	AC_MSG_NOTICE([Enabling Coverage (enable -O0 by default)])
	OPT_CFLAGS="-O0"
	COVERAGE_CFLAGS="-ftest-coverage -fprofile-arcs"
	COVERAGE_LDFLAGS="-ftest-coverage -fprofile-arcs"
	PACKAGE_FEATURES="$PACKAGE_FEATURES coverage"
else
	COVERAGE_CFLAGS=""
	COVERAGE_LDFLAGS=""
fi

if test "x${enable_small_memory_footprint}" = xyes ; then
	AC_DEFINE_UNQUOTED([HAVE_SMALL_MEMORY_FOOTPRINT], 1, [have small_memory_footprint])
	PACKAGE_FEATURES="$PACKAGE_FEATURES small-memory-footprint"
fi

if test "x${enable_ansi}" = xyes && \
		cc_supports_flag -std=iso9899:199409 ; then
	AC_MSG_NOTICE([Enabling ANSI Compatibility])
	ANSI_CPPFLAGS="-ansi -DANSI_ONLY"
	PACKAGE_FEATURES="$PACKAGE_FEATURES ansi"
else
	ANSI_CPPFLAGS=""
fi

if test "x${enable_fatal_warnings}" = xyes && \
		cc_supports_flag -Werror ; then
	AC_MSG_NOTICE([Enabling Fatal Warnings (-Werror)])
	WERROR_CFLAGS="-Werror"
	PACKAGE_FEATURES="$PACKAGE_FEATURES fatal-warnings"
else
	WERROR_CFLAGS=""
fi

# don't add addtional cflags
if test "x${enable_user_flags}" = xyes; then
  OPT_CFLAGS=""
  GDB_FLAGS=""
  EXTRA_WARNINGS=""
fi

if test "x${enable_secure_build}" = xyes; then
  # stolen from apache configure snippet
  AC_CACHE_CHECK([whether $CC accepts PIE flags], [ap_cv_cc_pie], [
    save_CFLAGS=$CFLAGS
    save_LDFLAGS=$LDFLAGS
    CFLAGS="$CFLAGS -fPIE"
    LDFLAGS="$LDFLAGS -pie"
    AC_RUN_IFELSE([AC_LANG_SOURCE([[static int foo[30000]; int main () { return 0; }]])],
      [ap_cv_cc_pie=yes], [ap_cv_cc_pie=no], [ap_cv_cc_pie=yes])
    CFLAGS=$save_CFLAGS
    LDFLAGS=$save_LDFLAGS
  ])
  if test "$ap_cv_cc_pie" = "yes"; then
    SEC_FLAGS="$SEC_FLAGS -fPIE"
    SEC_LDFLAGS="$SEC_LDFLAGS -pie"
    PACKAGE_FEATURES="$PACKAGE_FEATURES pie"
  fi

  # similar to above
  AC_CACHE_CHECK([whether $CC accepts RELRO flags], [ap_cv_cc_relro], [
    save_LDFLAGS=$LDFLAGS
    LDFLAGS="$LDFLAGS -Wl,-z,relro"
    AC_RUN_IFELSE([AC_LANG_SOURCE([[static int foo[30000]; int main () { return 0; }]])],
      [ap_cv_cc_relro=yes], [ap_cv_cc_relro=no], [ap_cv_cc_relro=yes])
    LDFLAGS=$save_LDFLAGS
  ])
  if test "$ap_cv_cc_relro" = "yes"; then
    SEC_LDFLAGS="$SEC_LDFLAGS -Wl,-z,relro"
    PACKAGE_FEATURES="$PACKAGE_FEATURES relro"
  fi

  AC_CACHE_CHECK([whether $CC accepts BINDNOW flags], [ap_cv_cc_bindnow], [
    save_LDFLAGS=$LDFLAGS
    LDFLAGS="$LDFLAGS -Wl,-z,now"
    AC_RUN_IFELSE([AC_LANG_SOURCE([[static int foo[30000]; int main () { return 0; }]])],
      [ap_cv_cc_bindnow=yes], [ap_cv_cc_bindnow=no], [ap_cv_cc_bindnow=yes])
    LDFLAGS=$save_LDFLAGS
  ])
  if test "$ap_cv_cc_bindnow" = "yes"; then
    SEC_LDFLAGS="$SEC_LDFLAGS -Wl,-z,now"
    PACKAGE_FEATURES="$PACKAGE_FEATURES bindnow"
  fi
fi

AC_CACHE_CHECK([whether $CC accepts "--as-needed"], [ap_cv_cc_as_needed], [
  save_LDFLAGS=$LDFLAGS
  LDFLAGS="$LDFLAGS -Wl,--as-needed"
  AC_RUN_IFELSE([AC_LANG_SOURCE([[static int foo[30000]; int main () { return 0; }]])],
    [ap_cv_cc_as_needed=yes], [ap_cv_cc_as_needed=no], [ap_cv_cc_as_needed=yes])
  LDFLAGS=$save_LDFLAGS
])

AC_CACHE_CHECK([whether $CC accepts "--version-script"], [ap_cv_cc_version_script], [
  save_LDFLAGS=$LDFLAGS
  LDFLAGS="$LDFLAGS -Wl,--version-script=conftest.versions"
  echo "CONFTEST { };" >conftest.versions
  AC_RUN_IFELSE([AC_LANG_SOURCE([[static int foo[30000]; int main () { return 0; }]])],
    [ap_cv_cc_version_script=yes], [ap_cv_cc_version_script=no], [ap_cv_cc_version_script=yes])
  rm -f conftest.versions
  LDFLAGS=$save_LDFLAGS
])
if test "$ap_cv_cc_version_script" = "yes"; then
  AC_SUBST(VERSCRIPT_LDFLAGS, ["-Wl,--version-script=\$(srcdir)/lib\$(call get_libname,\$<).versions"])
else
  AC_SUBST(VERSCRIPT_LDFLAGS, [""])
fi

# define global include dirs
INCLUDE_DIRS="$INCLUDE_DIRS -I\$(top_builddir)/include -I\$(top_srcdir)/include"
INCLUDE_DIRS="$INCLUDE_DIRS -I\$(top_builddir)/include/corosync -I\$(top_srcdir)/include/corosync"

# final build of *FLAGS
CFLAGS="$SANITIZERS_CFLAGS $ENV_CFLAGS $lt_prog_compiler_pic $SEC_FLAGS $OPT_CFLAGS $GDB_FLAGS \
	$COVERAGE_CFLAGS $EXTRA_WARNINGS \
	$WERROR_CFLAGS $LIBQB_CFLAGS \
	$SNMP_INCLUDES"
CPPFLAGS="$ENV_CPPFLAGS $ANSI_CPPFLAGS $INCLUDE_DIRS"
LDFLAGS="$SANITIZERS_LDFLAGS $ENV_LDFLAGS $lt_prog_compiler_pic $SEC_LDFLAGS $COVERAGE_LDFLAGS"

if test "$ap_cv_cc_as_needed" = "yes"; then
  LDFLAGS="$LDFLAGS -Wl,--as-needed"
fi

# substitute what we need:
AC_SUBST([BASHPATH])
AC_SUBST([INITDDIR])
AC_SUBST([SYSTEMDDIR])
AC_SUBST([LOGDIR])
AC_SUBST([LOGROTATEDIR])

AC_SUBST([SOMAJOR])
AC_SUBST([SOMINOR])
AC_SUBST([SOMICRO])
AC_SUBST([SONAME])
AC_SUBST([RUST_FLAGS])
AC_SUBST([RUST_TARGET_DIR])

AM_CONDITIONAL(INSTALL_MIB, test "${do_snmp}" = "1")
AM_CONDITIONAL(INSTALL_DBUSCONF, test "${enable_dbus}" = "yes")
AM_CONDITIONAL(AUGTOOL, test -n "${AUGTOOL}")

AM_CONDITIONAL(BUILD_HTML_DOCS, test -n "${GROFF}")

AC_SUBST([LINT_FLAGS])

AC_DEFINE_UNQUOTED([LOCALSTATEDIR], "$(eval echo ${localstatedir})", [localstate directory])

COROSYSCONFDIR=${sysconfdir}/corosync
AC_SUBST([COROSYSCONFDIR])
AC_DEFINE_UNQUOTED([COROSYSCONFDIR], "$(eval echo ${COROSYSCONFDIR})", [corosync config directory])

AC_DEFINE_UNQUOTED([PACKAGE_FEATURES], "${PACKAGE_FEATURES}", [corosync built-in features])

AC_OUTPUT

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Man pages                = ${mandir}])
AC_MSG_RESULT([  Doc dir                  = ${docdir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  Arch-independent files   = ${datadir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([  System init.d directory  = ${INITDDIR}])
AC_MSG_RESULT([  System systemd directory = ${SYSTEMDDIR}])
AC_MSG_RESULT([  Log directory            = ${LOGDIR}])
AC_MSG_RESULT([  Log rotate directory     = ${LOGROTATEDIR}])
AC_MSG_RESULT([  corosync config dir      = ${COROSYSCONFDIR}])
AC_MSG_RESULT([  init config directory    = ${INITCONFIGDIR}])
AC_MSG_RESULT([  Features                 =${PACKAGE_FEATURES}])
AC_MSG_RESULT([  Rust bindings            = ${enable_rust_bindings}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Library SONAME           = ${SONAME}])
LIB_MSG_RESULT(m4_shift(local_soname_list))dnl
AC_MSG_RESULT([  Default optimization     = ${OPT_CFLAGS}])
AC_MSG_RESULT([  Default debug options    = ${GDB_FLAGS}])
AC_MSG_RESULT([  Extra compiler warnings  = ${EXTRA_WARNING}])
AC_MSG_RESULT([  Env. defined CFLAG       = ${ENV_CFLAGS}])
AC_MSG_RESULT([  Env. defined CPPFLAGS    = ${ENV_CPPFLAGS}])
AC_MSG_RESULT([  Env. defined LDFLAGS     = ${ENV_LDFLAGS}])
AC_MSG_RESULT([  ANSI defined CPPFLAGS    = ${ANSI_CPPFLAGS}])
AC_MSG_RESULT([  Coverage     CFLAGS      = ${COVERAGE_CFLAGS}])
AC_MSG_RESULT([  Coverage     LDFLAGS     = ${COVERAGE_LDFLAGS}])
AC_MSG_RESULT([  Fatal War.   CFLAGS      = ${WERROR_CFLAGS}])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
