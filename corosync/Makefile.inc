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
	OPENAIS_COMPAT=LINUX
endif
ifeq "$(UNAME)" "Darwin"
	OPENAIS_COMPAT=DARWIN
	CP=rsync
endif
ifneq "" "$(findstring BSD,$(UNAME))"
	OPENAIS_COMPAT=BSD
endif
ifeq "$(UNAME)" "SunOS"
	OPENAIS_COMPAT=SOLARIS
	# Note that CC must be set to gcc compiled to link with gnu-ld 
endif
ifndef OPENAIS_COMPAT
	$(error "OPENAIS_COMPAT cannot be detected, it must be manually defined")
endif

# BUILD_DYNAMIC can be defined to 1 to build for dynamic loading of service
#	handler modules.  If the developer intends to debug, building without
#	dynamic modules should provide an easier route.
ifndef BUILD_DYNAMIC
	BUILD_DYNAMIC=1
endif

# OPENAIS_BUILD can be defined as RELEASE or DEBUG
#
ifndef OPENAIS_BUILD
	OPENAIS_BUILD=DEBUG
endif

# OPENAIS_PROFILE

# default CFLAGS, LDFLAGS
#
CFLAGS =
LDFLAGS =
DYFLAGS =

# Adding the TS_CLASS flag enables not being scheduled RR
#CFLAGS += -DTS_CLASS

# build CFLAGS, LDFLAGS
#
ifeq (${OPENAIS_BUILD}, RELEASE) 
	CFLAGS += -O3 -Wall
# -Wstrict-aliasing=2 TODO sameday fix all of these
ifndef OPENAIS_PROFILE
	CFLAGS += -fomit-frame-pointer
endif
	LDFLAGS += 
endif
ifeq (${OPENAIS_BUILD}, DEBUG) 
	CFLAGS += -O0 -g -Wall -DDEBUG
	LDFLAGS += -g
	ifeq (${OPENAIS_COMPAT}, SOLARIS)
		CFLAGS += -Werror -DTS_CLASS
	endif
endif
ifeq (${OPENAIS_BUILD}, COVERAGE)
	CFLAGS += -O0 -g -ftest-coverage -fprofile-arcs
	LDFLAGS += -g -ftest-coverage -fprofile-arcs
	BUILD_DYNAMIC=0
endif

ifdef OPENAIS_PROFILE
	CFLAGS += -pg
	LDFLAGS += -pg
endif

# platform specific CFLAGS, LDFLAGS
#
ifeq (${OPENAIS_COMPAT}, LINUX) 
	override CFLAGS += -DOPENAIS_LINUX
	override LDFLAGS += -ldl -lpthread
	override DYFLAGS += -rdynamic
endif
ifeq (${OPENAIS_COMPAT}, BSD)
	override CFLAGS += -DOPENAIS_BSD
	override LDFLAGS += -pthread
	override DYFLAGS += -export-dynamic
endif
ifeq (${OPENAIS_COMPAT}, DARWIN)
	override CFLAGS += -DOPENAIS_DARWIN
	override LDFLAGS += -lpthread
endif
ifeq (${OPENAIS_COMPAT}, SOLARIS)
	override CFLAGS += -DOPENAIS_SOLARIS -D_REENTRANT
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

