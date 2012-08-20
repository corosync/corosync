#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# bootstrap / init
AC_PREREQ([2.61])

AC_INIT([corosync],
	m4_esyscmd([build-aux/git-version-gen .tarball-version]),
	[openais@lists.osdl.org])

AM_INIT_AUTOMAKE([-Wno-portability])

AC_CONFIG_SRCDIR([lib/coroipcc.c])
AC_CONFIG_HEADER([include/corosync/config.h include/corosync/cs_config.h])

AC_CANONICAL_HOST

AC_LANG([C])

dnl Fix default variables - "prefix" variable if not specified
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
	dnl Fix "libdir" variable if not specified
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

if test "$srcdir" = "."; then
	AC_MSG_NOTICE([building in place srcdir:$srcdir])
	AC_DEFINE([BUILDING_IN_PLACE], 1, [building in place])
else
	AC_MSG_NOTICE([building out of tree srcdir:$srcdir])
fi

# Checks for programs.

# check stolen from gnulib/m4/gnu-make.m4
if ! ${MAKE-make} --version /cannot/make/this >/dev/null 2>&1; then
	AC_MSG_ERROR([you don't seem to have GNU make; it is required])
fi

sinclude(coroysync-default.m4)

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_CHECK_PROGS([GROFF], [groff])
AC_CHECK_PROGS([PKGCONFIG], [pkg-config])

# Checks for libraries.
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([socket], [socket])
AC_CHECK_LIB([nsl], [t_open])
AC_CHECK_LIB([rt], [sched_getscheduler])

# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h \
		  stdlib.h string.h sys/ioctl.h sys/param.h sys/socket.h \
		  sys/time.h syslog.h unistd.h sys/types.h getopt.h malloc.h \
		  sys/sockio.h net/if_var.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_REPLACE_FNMATCH
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([alarm alphasort atexit bzero dup2 endgrent endpwent fcntl \
		getcwd getpeerucred getpeereid gettimeofday inet_ntoa memmove \
		memset mkdir scandir select socket strcasecmp strchr strdup \
		strerror strrchr strspn strstr pthread_setschedparam \
		sched_get_priority_max sched_setscheduler])

AC_CONFIG_FILES([Makefile
		 exec/Makefile
		 include/Makefile
		 init/Makefile
		 lcr/Makefile
		 lib/Makefile
		 man/Makefile
		 pkgconfig/Makefile
		 services/Makefile
		 test/Makefile
		 tools/Makefile
		 conf/Makefile])

### Local business

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

## helper for CC stuff
cc_supports_flag() {
	local CFLAGS="$@"
	AC_MSG_CHECKING([whether $CC supports "$@"])
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(){return 0;}])] ,
			  [RC=0; AC_MSG_RESULT([yes])],
			  [RC=1; AC_MSG_RESULT([no])])
	return $RC
}

## cleanup
AC_MSG_NOTICE(Sanitizing prefix: ${prefix})
case $prefix in
  NONE) prefix=/usr/local;;
esac

AC_MSG_NOTICE(Sanitizing exec_prefix: ${exec_prefix})
case $exec_prefix in
  dnl For consistency with Corosync, map NONE->$prefix
  NONE)   exec_prefix=$prefix;;
  prefix) exec_prefix=$prefix;;
esac

## local defines
PACKAGE_FEATURES=""

LINT_FLAGS="-weak -unrecog +posixlib +ignoresigns -fcnuse \
	-badflag -D__gnuc_va_list=va_list -D__attribute\(x\)="

# default libraries SONAME
SOMAJOR="4"
SOMINOR="0"
SOMICRO="0"
SONAME="${SOMAJOR}.${SOMINOR}.${SOMICRO}"

# specific libraries SONAME
LIB_SONAME_IMPORT([cfg])
LIB_SONAME_IMPORT([confdb])
LIB_SONAME_IMPORT([cpg])
LIB_SONAME_IMPORT([evs])
LIB_SONAME_IMPORT([pload])
LIB_SONAME_IMPORT([quorum])
LIB_SONAME_IMPORT([sam])
LIB_SONAME_IMPORT([votequorum])

# local options
AC_ARG_ENABLE([ansi],
	[  --enable-ansi           : force to build with ANSI standards. ],
	[ default="no" ])

AC_ARG_ENABLE([fatal-warnings],
	[  --enable-fatal-warnings : enable fatal warnings. ],
	[ default="no" ])

AC_ARG_ENABLE([debug],
	[ --enable-debug          : enable debug build. ],
	[ default="no" ])

AC_ARG_ENABLE([user-flags],
	[  --enable-user-flags     : rely on user environment. ],
	[ default="no" ])

AC_ARG_ENABLE([coverage],
	[  --enable-coverage       : coverage analysis of the codebase. ],
	[ default="no" ])

AC_ARG_ENABLE([small-memory-footprint],
	[  --enable-small-memory-footprint : Use small message queues and small messages sizes. ],
	[ default="no" ])

AC_ARG_ENABLE([nss],
	[  --enable-nss            : Network Security Services encryption. ],,
	[ enable_nss="yes" ])

AC_ARG_ENABLE([dbus],
	[  --enable-dbus                    : dbus events. ],,
	[ enable_dbus="no" ])

AC_ARG_ENABLE([testagents],
	[  --enable-testagents             : Install Test Agents. ],,
	[ default="no" ])

AC_ARG_ENABLE([rdma],
	[  --enable-rdma           : Infiniband RDMA transport support ],,
	[ enable_rdma="no" ])
AM_CONDITIONAL(BUILD_RDMA, test x$enable_rdma = xyes)

AC_ARG_WITH([initddir],
	[  --with-initddir=DIR     : path to init script directory. ],
	[ INITDDIR="$withval" ],
	[ INITDDIR="$sysconfdir/init.d" ])

AC_ARG_WITH([lcrso-dir],
	[  --with-lcrso-dir=DIR    : corosync lcrso files. ],
	[ LCRSODIR="$withval" ],
	[ LCRSODIR="$libexecdir/lcrso" ])

AC_ARG_WITH([socket-dir],
	[  --with-socket-dir=DIR   : corosync socket dir. ],
	[ SOCKETDIR="$withval" ],
	[ SOCKETDIR="$localstatedir/run" ])

AC_ARG_ENABLE([snmp],
       [  --enable-snmp           : SNMP protocol support ],
       [ default="no" ])

# OS detection
# THIS SECTION MUST DIE!
CP=cp
CP_ARGS="-a"
OS_LDL="-ldl"
case "$host_os" in
	*linux*)
		AC_DEFINE_UNQUOTED([COROSYNC_LINUX], [1],
				   [Compiling for Linux platform])
		OS_CFLAGS=""
		OS_CPPFLAGS="-D_GNU_SOURCE"
		OS_LDFLAGS=""
		OS_DYFLAGS="-rdynamic"
		DARWIN_OPTS=""
	;;
	darwin*)
		AC_DEFINE_UNQUOTED([COROSYNC_DARWIN], [1],
				   [Compiling for Darwin platform])
		CP=rsync
		OS_CFLAGS=""
		OS_CPPFLAGS=""
		OS_LDFLAGS=""
		OS_DYFLAGS=""
		DARWIN_OPTS="-dynamiclib -bind_at_load \
			     -current_version ${SONAME} \
			     -compatibility_version ${SONAME} -install_name \$(libdir)/\$(@)"
		AC_DEFINE_UNQUOTED([MAP_ANONYMOUS], [MAP_ANON],
				   [Shared memory define for Darwin platform])
		AC_DEFINE_UNQUOTED([PATH_MAX], [4096],
				   [Number of chars in a path name including nul])
		AC_DEFINE_UNQUOTED([NAME_MAX], [255],
				   [Number of chars in a file name])
	;;
	*bsd*)
		AC_DEFINE_UNQUOTED([COROSYNC_BSD], [1],
				   [Compiling for BSD platform])
		AC_DEFINE_UNQUOTED([MAP_ANONYMOUS], [MAP_ANON],
				   [Shared memory define for Darwin platform])
		OS_CFLAGS=""
		OS_CPPFLAGS="-I/usr/local/include"
		OS_LDFLAGS="-L/usr/local/lib"
		OS_DYFLAGS="-export-dynamic"
		DARWIN_OPTS=""
		OS_LDL=""
		case "$host_os" in
			*freebsd[[234567]]*)
			;;
			*freebsd*)
				AC_DEFINE_UNQUOTED([COROSYNC_FREEBSD_GE_8], [1],
					   [Compiling for FreeBSD >= 8 platform])
			;;
			*netbsd*)
				AC_DEFINE_UNQUOTED([_SEM_SEMUN_UNDEFINED], [1],
					   [The semun structure is undefined])
				CP_ARGS="-RpP"
			;;
		esac
	;;
	*solaris*)
		AC_DEFINE_UNQUOTED([COROSYNC_SOLARIS], [1],
				   [Compiling for Solaris platform])
		AC_DEFINE_UNQUOTED([TS_CLASS], [1],
				   [Prevent being scheduled RR])
		AC_DEFINE_UNQUOTED([_SEM_SEMUN_UNDEFINED], [1],
				   [The semun structure is undefined])
		CP=rsync
		OS_CFLAGS=""
		OS_CPPFLAGS="-D_REENTRANT"
		OS_LDFLAGS=""
		OS_DYFLAGS="-Wl,-z,lazyload"
		DARWIN_OPTS=""
		SOLARIS_OPTS=" "
	;;
	*)
		AC_MSG_ERROR([Unsupported OS? hmmmm])
	;;
esac

AC_SUBST(CP)
AC_SUBST(CP_ARGS)
# *FLAGS handling goes here

ENV_CFLAGS="$CFLAGS"
ENV_CPPFLAGS="$CPPFLAGS"
ENV_LDFLAGS="$LDFLAGS"

# debug build stuff
if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Compiling Debugging code])
	OPT_CFLAGS="-O0"
	PACKAGE_FEATURES="$PACKAGE_FEATURES debug"
else
	OPT_CFLAGS="-O3"
fi

# gdb flags
if test "x${GCC}" = xyes; then
	GDB_FLAGS="-ggdb3"
else
	GDB_FLAGS="-g"
fi

# Look for libnss
if test "x${enable_nss}" = xyes; then
	PKG_CHECK_MODULES([nss],[nss])
	AC_DEFINE_UNQUOTED([HAVE_LIBNSS], 1, [have libnss])
	PACKAGE_FEATURES="$PACKAGE_FEATURES nss"
fi

# Look for dbus-1
if test "x${enable_dbus}" = xyes; then
	PKG_CHECK_MODULES([DBUS],[dbus-1])
	AC_DEFINE_UNQUOTED([HAVE_DBUS], 1, [have dbus])
	PACKAGE_FEATURES="$PACKAGE_FEATURES dbus"
fi

if test "x${enable_testagents}" = xyes; then
	AC_DEFINE_UNQUOTED([HAVE_TESTAGENTS], 1, [have testagents])
	PACKAGE_FEATURES="$PACKAGE_FEATURES testagents"
fi

if test "x${enable_rdma}" = xyes; then
	PKG_CHECK_MODULES([rdmacm],[rdmacm])
	PKG_CHECK_MODULES([ibverbs],[ibverbs])
	AC_DEFINE_UNQUOTED([HAVE_RDMA], 1, [have rdmacm])
	PACKAGE_FEATURES="$PACKAGE_FEATURES rdma"
fi

if test "x${enable_monitoring}" = xyes; then

	AC_CHECK_LIB([statgrab], [sg_get_mem_stats], have_libstatgrab="yes", have_libstatgrab="no")

	if test "x${have_libstatgrab}" = xyes; then
		AC_DEFINE_UNQUOTED([HAVE_LIBSTATGRAB], 1, [have libstatgrab])
		statgrab_LIBS="-lstatgrab"
	else
		if test "x${have_linux}" = xno; then
			AC_MSG_ERROR(monitoring requires libstatgrab on non-linux systems)
		fi
	fi
	AC_SUBST([statgrab_LIBS])
	AC_DEFINE_UNQUOTED([HAVE_MONITORING], 1, [have resource monitoring])
	PACKAGE_FEATURES="$PACKAGE_FEATURES monitoring"
fi

if test "x${enable_watchdog}" = xyes; then
	AC_CHECK_HEADER(linux/watchdog.h,,AC_MSG_ERROR(watchdog requires linux/watchdog.h))
	AC_CHECK_HEADER(linux/reboot.h,,AC_MSG_ERROR(watchdog requires linux/reboot.h))
	AC_DEFINE_UNQUOTED([HAVE_WATCHDOG], 1, [have watchdog])
	PACKAGE_FEATURES="$PACKAGE_FEATURES watchdog"
fi

if test "x${enable_augeas}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES augeas"
fi

if test "x${enable_snmp}" = xyes; then
       SNMPCONFIG=""
       AC_CHECK_HEADERS(net-snmp/net-snmp-config.h)

       if test "x${ac_cv_header_net_snmp_net_snmp_config_h}" != "xyes"; then
               enable_snmp=no
       fi

       if test $enable_snmp != no; then
               AC_PATH_PROGS(SNMPCONFIG, net-snmp-config)
               if test "X${SNMPCONFIG}" = "X"; then
                       AC_MSG_RESULT(You need the net_snmp development package to continue.)
                       enable_snmp=no
               fi
       fi

       if test $enable_snmp != no; then
               AC_MSG_CHECKING(for special snmp libraries)
               SNMPLIBS=`$SNMPCONFIG --libs`
               AC_MSG_RESULT($SNMPLIBS)
       fi

       if test $enable_snmp != no; then
               savedLibs=$LIBS
               LIBS="$LIBS $SNMPLIBS"
               AC_CHECK_FUNCS(netsnmp_transport_open_client)
               if test $ac_cv_func_netsnmp_transport_open_client != yes; then
                       AC_CHECK_FUNCS(netsnmp_tdomain_transport)
                       if test $ac_cv_func_netsnmp_tdomain_transport != yes; then
                               enable_snmp=no
                       fi
               else
                       AC_DEFINE_UNQUOTED([NETSNMPV54], $NETSNMP_NEW_SUPPORT, [have net-snmp5.4 over])
               fi
               LIBS=$savedLibs
       fi

       AC_MSG_CHECKING(for snmp)
       AC_MSG_RESULT($enable_snmp)
   if test $enable_snmp = no; then
               enable_snmp=0
               AC_MSG_ERROR(Unable to support SNMP)
   else
               enable_snmp=1
               PACKAGE_FEATURES="$PACKAGE_FEATURES snmp"
               AC_DEFINE_UNQUOTED([ENABLE_SNMP], $enable_snmp, [Build in support for sending SNMP traps])
   fi
else
       enable_snmp=0
fi
AC_SUBST([SNMPLIBS])
AC_SUBST([SNMP_LCRSO])
AM_CONDITIONAL(BUILD_SNMP, test "${enable_snmp}" = "1")

# extra warnings
EXTRA_WARNINGS=""

WARNLIST="
	all
	shadow
	missing-prototypes
	missing-declarations
	strict-prototypes
	declaration-after-statement
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
	gnu89-inline
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
	ANSI_CPPFLAGS="-ansi -D_GNU_SOURCE -DANSI_ONLY"
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

# final build of *FLAGS
CFLAGS="$ENV_CFLAGS $OPT_CFLAGS $GDB_FLAGS $OS_CFLAGS \
	$COVERAGE_CFLAGS $EXTRA_WARNINGS $WERROR_CFLAGS $NSS_CFLAGS"
CPPFLAGS="$ENV_CPPFLAGS $ANSI_CPPFLAGS $OS_CPPFLAGS"
LDFLAGS="$ENV_LDFLAGS $COVERAGE_LDFLAGS $OS_LDFLAGS"

# substitute what we need:
AC_SUBST([INITDDIR])
AC_SUBST([LCRSODIR])
AC_SUBST([SOCKETDIR])

AC_SUBST([SOMAJOR])
AC_SUBST([SOMINOR])
AC_SUBST([SOMICRO])
AC_SUBST([SONAME])
AC_SUBST([COVERAGE_LCRSO_EXTRA_LDFLAGS])
AC_SUBST([OS_DYFLAGS])

AC_SUBST([OS_LDL])
AM_CONDITIONAL(INSTALL_MIB, test "${enable_snmp}" = "1")
AM_CONDITIONAL(INSTALL_DBUSCONF, test "${enable_dbus}" = "1")
AC_SUBST([NSS_LDFLAGS])

AM_CONDITIONAL(BUILD_DARWIN, test -n "${DARWIN_OPTS}")
AM_CONDITIONAL(BUILD_SOLARIS, test -n "${SOLARIS_OPTS}")
AC_SUBST([DARWIN_OPTS])
AC_SUBST([SOLARIS_OPTS])

AM_CONDITIONAL(BUILD_HTML_DOCS, test -n "${GROFF}")

AC_SUBST([LINT_FLAGS])

AC_DEFINE_UNQUOTED([LCRSODIR], "$(eval echo ${LCRSODIR})", [LCRSO directory])
AC_DEFINE_UNQUOTED([SOCKETDIR], "$(eval echo ${SOCKETDIR})", [Socket directory])
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
AC_MSG_RESULT([  corosync config dir      = ${COROSYSCONFDIR}])
AC_MSG_RESULT([  LCRSO                    = ${LCRSODIR}])
AC_MSG_RESULT([  SOCKETDIR                = ${SOCKETDIR}])
AC_MSG_RESULT([  Features                 =${PACKAGE_FEATURES}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Library SONAME           = ${SONAME}])
LIB_MSG_RESULT(m4_shift(local_soname_list))dnl
AC_MSG_RESULT([  Default optimization     = ${OPT_CFLAGS}])
AC_MSG_RESULT([  Default debug options    = ${GDB_CFLAGS}])
AC_MSG_RESULT([  Extra compiler warnings  = ${EXTRA_WARNING}])
AC_MSG_RESULT([  Env. defined CFLAG       = ${ENV_CFLAGS}])
AC_MSG_RESULT([  Env. defined CPPFLAGS    = ${ENV_CPPFLAGS}])
AC_MSG_RESULT([  Env. defined LDFLAGS     = ${ENV_LDFLAGS}])
AC_MSG_RESULT([  OS   defined CFLAGS      = ${OS_CFLAGS}])
AC_MSG_RESULT([  OS   defined CPPFLAGS    = ${OS_CPPFLAGS}])
AC_MSG_RESULT([  OS   defined LDFLAGS     = ${OS_LDFLAGS}])
AC_MSG_RESULT([  OS   defined LDL         = ${OS_LDL}])
AC_MSG_RESULT([  OS   defined DYFLAGS     = ${OS_DYFLAGS}])
AC_MSG_RESULT([  ANSI defined CPPFLAGS    = ${ANSI_CPPFLAGS}])
AC_MSG_RESULT([  Coverage     CFLAGS      = ${COVERAGE_CFLAGS}])
AC_MSG_RESULT([  Coverage     LDFLAGS     = ${COVERAGE_LDFLAGS}])
AC_MSG_RESULT([  Fatal War.   CFLAGS      = ${WERROR_CFLAGS}])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
