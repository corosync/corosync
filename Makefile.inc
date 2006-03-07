# Basic OS detection
#
UNAME=$(shell uname)

ifeq "$(UNAME)" "Linux"
	OPENAIS_COMPAT=LINUX
endif
ifeq "$(UNAME)" "Darwin"
	OPENAIS_COMPAT=DARWIN
endif
ifneq "" "$(findstring BSD,$(UNAME))"
	OPENAIS_COMPAT=BSD
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
	OPENAIS_BUILD=RELEASE
endif

# OPENAIS_PROFILE
# OPENAIS_COVERAGE

# OPENAIS_USER, OPENAIS_GROUP default to ais
#
ifndef OPENAIS_USER
	OPENAIS_USER=ais
endif
ifndef OPENAIS_GROUP
	OPENAIS_GROUP=ais
endif

# default CFLAGS, LDFLAGS
#
CFLAGS = 
LDFLAGS =
DYFLAGS =

# build CFLAGS, LDFLAGS
#
ifeq (${OPENAIS_BUILD}, RELEASE) 
	CFLAGS += -O3 -Wall
ifndef OPENAIS_PROFILE
	CFLAGS += -fomit-frame-pointer
endif
	LDFLAGS += 
endif
ifeq (${OPENAIS_BUILD}, DEBUG) 
	CFLAGS += -O0 -g -Wall
	LDFLAGS += -g
endif

ifdef OPENAIS_PROFILE
	CFLAGS += -pg
	LDFLAGS += -pg
endif
ifdef OPENAIS_COVERAGE
	CFLAGS += -ftest-coverage -fprofile-arcs
	LDFLAGS += -ftest-coverage -fprofile-arcs
endif

# platform specific CFLAGS, LDFLAGS
#
ifeq (${OPENAIS_COMPAT}, LINUX) 
	CFLAGS += -DOPENAIS_LINUX
	LDFLAGS += -ldl -lpthread
	DYFLAGS += -rdynamic
endif
ifeq (${OPENAIS_COMPAT}, BSD)
	CFLAGS += -DOPENAIS_BSD
	LDFLAGS += -pthread
	DYFLAGS += -export-dynamic
endif
ifeq (${OPENAIS_COMPAT}, DARWIN)
	CFLAGS += -DOPENAIS_DARWIN
	LDFLAGS += -lpthread
endif
