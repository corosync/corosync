#
# Copyright (c) 2009 Red Hat, Inc.
#
# Authors: Andrew Beekhof
#	   Steven Dake (sdake@redhat.com)
#
# This software licensed under BSD license, the text of which follows:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

# Functions
uc=$(shell echo $1 | tr a-z A-Z)
get_soname=$(if $($(call uc,$1)_SONAME),$($(call uc,$1)_SONAME),$(SONAME))
get_major=$(firstword $(subst ., ,$(call get_soname,$1)))
get_sharedlibs=$(foreach lib,$(SHARED_LIBS_SO:lib%.so=%),lib$(lib).so.$(call get_soname,$(lib)))
get_sharedlibs_two=$(foreach lib,$(SHARED_LIBS_SO:lib%.so=%),lib$(lib).so.$(call get_major,$(lib)))
get_explicit_sharedlibs=$(foreach lib,$(EXPLICIT_SHARED_LIBS_SO:lib%.so=%),lib$(lib).so.$(call get_soname,$(lib)))
get_explicit_sharedlibs_two=$(foreach lib,$(EXPLICIT_SHARED_LIBS_SO:lib%.so=%),lib$(lib).so.$(call get_major,$(lib)))
get_linker_add=$(if $($(call uc,$1)_LINKER_ADD),$($(call uc,$1)_LINKER_ADD))

MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS		= -fPIC

AM_LDFLAGS		= -lpthread

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= libcpg.a libconfdb.a libevs.a libcfg.a libquorum.a \
			  libvotequorum.a libpload.a libcoroipcc.a libsam.a
SHARED_LIBS_SO		= $(filter-out $(EXPLICIT_SHARED_LIBS_SO), $(lib_LIBRARIES:%.a=%.so))
EXPLICIT_SHARED_LIBS_SO = libcoroipcc.so

libcpg_a_SOURCES	= cpg.c
libcfg_a_SOURCES	= cfg.c
libevs_a_SOURCES	= evs.c
libpload_a_SOURCES	= pload.c
libquorum_a_SOURCES	= quorum.c
libvotequorum_a_SOURCES	= votequorum.c
libconfdb_a_SOURCES	= confdb.c sa-confdb.c
libconfdb_a_LIBADD	= ../lcr/lcr_ifact.o
CONFDB_LINKER_ADD	= $(OS_DYFLAGS) $(OS_LDL)
libcoroipcc_a_SOURCES	= coroipcc.c
libsam_a_SOURCES	= sam.c

noinst_HEADERS		= sa-confdb.h util.h \
			  libcfg.versions libconfdb.versions \
			  libcoroipcc.versions libcpg.versions \
			  libevs.versions libpload.versions \
			  libquorum.versions libvotequorum.versions \
			  libsam.versions

../lcr/lcr_ifact.o:
	$(MAKE) -C ../lcr lcr_ifact.o

if BUILD_DARWIN

libcoroipcc.so.$(SONAME): coroipcc.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipcc.o -o $@ $(AM_LDFLAGS)
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so.$(SOMAJOR)

lib%.so: lib%.a libcoroipcc.so.$(SONAME)
	$(CC) $(DARWIN_OPTS) $(call get_linker_add,$*) -Wl,-whole-archive $^ -Wl,-no-whole-archive -o $@
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so.$(call get_major,$*)

else

if BUILD_SOLARIS

libcoroipcc.so.$(SONAME): coroipcc.o
	$(LD) $(LDFLAGS) $(SOLARIS_OPTS) -G coroipcc.o -o $@ $(AM_LDFLAGS) -lsocket -lnsl
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so.$(SOMAJOR)

lib%.so.$(SONAME): lib%.a libcoroipcc.so.$(SONAME)
	$(LD) $(SOLARIS_OPTS) $(call get_linker_add,$*) -G -whole-archive $^ -no-whole-archive -o $@
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so.$(call get_major,$*)

else

libcoroipcc.so.$(SONAME): coroipcc.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libcoroipcc.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libcoroipcc.versions \
		$(LDFLAGS) $^ $(AM_LDFLAGS)
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so.$(SOMAJOR)

lib%.so: lib%.a libcoroipcc.so.$(SONAME)
	$(CC) -shared -o $@.$(call get_soname,$*) \
		-Wl,-soname=lib$*.so.$(call get_major,$*) \
		-Wl,-version-script=$(srcdir)/lib$*.versions \
		-Wl,-whole-archive $^ -Wl,-no-whole-archive $(LDFLAGS) $(AM_LDFLAGS) $(call get_linker_add,$*)
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so.$(call get_major,$*)

endif

endif

all-local: $(get_explicit_sharedlibs) $(SHARED_LIBS_SO)
	@echo Built shared libs

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(get_sharedlibs) $(get_explicit_sharedlibs) $(DESTDIR)/$(libdir)
	$(CP) $(CP_ARGS) $(SHARED_LIBS_SO) $(EXPLICIT_SHARED_LIBS_SO) $(get_sharedlibs_two) $(get_explicit_sharedlibs_two) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir)/ && \
		rm -f $(get_sharedlibs) $(SHARED_LIBS_SO) $(get_sharedlibs_two)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg
