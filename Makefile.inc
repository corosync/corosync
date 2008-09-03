# Common options
PREFIX=/usr
DESTDIR=
LCRSODIR=$(PREFIX)/libexec/lcrso

# Do not modify below this line

# Basic OS detection
#
UNAME=$(shell uname)
CP=cp

ifeq "$(UNAME)" "Linux"
	COROSYNC_COMPAT=LINUX
endif
ifeq "$(UNAME)" "Darwin"
	COROSYNC_COMPAT=DARWIN
	CP=rsync
endif
ifneq "" "$(findstring BSD,$(UNAME))"
	COROSYNC_COMPAT=BSD
endif
ifeq "$(UNAME)" "SunOS"
	COROSYNC_COMPAT=SOLARIS
	# Note that CC must be set to gcc compiled to link with gnu-ld 
endif
ifndef COROSYNC_COMPAT
	$(error "COROSYNC_COMPAT cannot be detected, it must be manually defined")
endif

# BUILD_DYNAMIC can be defined to 1 to build for dynamic loading of service
#	handler modules.  If the developer intends to debug, building without
#	dynamic modules should provide an easier route.
ifndef BUILD_DYNAMIC
	BUILD_DYNAMIC=1
endif

# COROSYNC_BUILD can be defined as RELEASE or DEBUG
#
ifndef COROSYNC_BUILD
	COROSYNC_BUILD=DEBUG
endif

# COROSYNC_PROFILE

# default CFLAGS, LDFLAGS
#
CFLAGS =
LDFLAGS =
DYFLAGS =

override CFLAGS += -DLCRSODIR='"$(LCRSODIR)"'


# Adding the TS_CLASS flag enables not being scheduled RR
#CFLAGS += -DTS_CLASS

# build CFLAGS, LDFLAGS
#
ifeq (${COROSYNC_BUILD}, RELEASE) 
	CFLAGS += -O3 -Wall
# -Wstrict-aliasing=2 TODO sameday fix all of these
ifndef COROSYNC_PROFILE
	CFLAGS += -fomit-frame-pointer
endif
	LDFLAGS += 
endif
ifeq (${COROSYNC_BUILD}, DEBUG) 
	CFLAGS += -O0 -g -Wall -DDEBUG --time
	LDFLAGS += -g
	ifeq (${COROSYNC_COMPAT}, SOLARIS)
		CFLAGS += -Werror -DTS_CLASS
	endif
endif
ifeq (${COROSYNC_BUILD}, COVERAGE)
	CFLAGS += -O0 -g -ftest-coverage -fprofile-arcs
	LDFLAGS += -g -ftest-coverage -fprofile-arcs
	BUILD_DYNAMIC=0
endif

ifdef COROSYNC_PROFILE
	CFLAGS += -pg
	LDFLAGS += -pg
endif

# platform specific CFLAGS, LDFLAGS
#
ifeq (${COROSYNC_COMPAT}, LINUX) 
	override CFLAGS += -DCOROSYNC_LINUX
	override LDFLAGS += -ldl -lpthread
	override DYFLAGS += -rdynamic
endif
ifeq (${COROSYNC_COMPAT}, BSD)
	override CFLAGS += -DCOROSYNC_BSD
	override LDFLAGS += -pthread
	override DYFLAGS += -export-dynamic
endif
ifeq (${COROSYNC_COMPAT}, DARWIN)
	override CFLAGS += -DCOROSYNC_DARWIN
	override LDFLAGS += -lpthread
endif
ifeq (${COROSYNC_COMPAT}, SOLARIS)
	override CFLAGS += -DCOROSYNC_SOLARIS -D_REENTRANT
	override LDFLAGS += -lpthread
	# See http://sources.redhat.com/ml/bug-gnu-utils/2000-07/msg00168.html
	override LDFLAGS += -Wl,--export-dynamic -Wl,-rpath-link=/usr/lib
ifeq ($(shell uname -r), 5.10)
	override CFLAGS += -DHAVE_GETPEERUCRED -DHAVE_SCANDIR -DHAVE_ALPHASORT
endif
ifeq ($(shell uname -r), 5.11)
	override CFLAGS += -DHAVE_GETPEERUCRED -DHAVE_SCANDIR -DHAVE_ALPHASORT
endif
endif

VPATH:=. $(srcdir) $(srcdir)$(subdir)
override CFLAGS += -I$(srcdir)include -I$(srcdir)$(subdir)

