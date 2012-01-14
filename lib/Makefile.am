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
get_linker_add=$(if $($(call uc,$1)_LINKER_ADD),$($(call uc,$1)_LINKER_ADD))

MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS		= -fPIC

AM_LDFLAGS		= -lpthread

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= libcpg.a libquorum.a libevs.a libcfg.a \
			  libvotequorum.a libpload.a libsam.a libcmap.a
SHARED_LIBS_SO		= $(lib_LIBRARIES:%.a=%.so)

libcpg_a_SOURCES	= cpg.c
libcfg_a_SOURCES	= cfg.c
libevs_a_SOURCES	= evs.c
libpload_a_SOURCES	= pload.c
libquorum_a_SOURCES	= quorum.c
libvotequorum_a_SOURCES	= votequorum.c
libcmap_a_SOURCES	= cmap.c
SAM_LINKER_ADD		= -L. -lquorum -lcmap
libsam_a_SOURCES	= sam.c

noinst_HEADERS		= util.h \
			  libcfg.versions \
			  libcpg.versions \
			  libevs.versions libpload.versions \
			  libquorum.versions libvotequorum.versions \
			  libsam.versions libcmap.versions

if BUILD_DARWIN

lib%.so: lib%.a $(LIBQB_LIBS)
	$(CC) $(DARWIN_OPTS) $(call get_linker_add,$*) -Wl,-whole-archive $^ -Wl,-no-whole-archive -o $@
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so.$(call get_major,$*)

else

if BUILD_SOLARIS

lib%.so.$(SONAME): lib%.a
	$(LD) $(SOLARIS_OPTS) $(call get_linker_add,$*) -G -whole-archive $^ -no-whole-archive -o $@
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so.$(call get_major,$*)

else

lib%.so: lib%.a
	$(CC) -shared -o $@.$(call get_soname,$*) \
		-Wl,-soname=lib$*.so.$(call get_major,$*) \
		-Wl,-version-script=$(srcdir)/lib$*.versions \
		-Wl,-whole-archive $^ -Wl,-no-whole-archive $(LDFLAGS) $(LIBQB_LIBS) $(AM_LDFLAGS) $(call get_linker_add,$*)
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so
	ln -sf lib$*.so.$(call get_soname,$*) lib$*.so.$(call get_major,$*)

endif

endif

all-local: $(get_explicit_sharedlibs) $(SHARED_LIBS_SO)
	@echo Built shared libs

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(get_sharedlibs) $(DESTDIR)/$(libdir)
	$(CP) -a $(SHARED_LIBS_SO) $(get_sharedlibs_two) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir)/ && \
		rm -f $(get_sharedlibs) $(SHARED_LIBS_SO) $(get_sharedlibs_two)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg
