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

MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS		= -fPIC
AM_CPPFLAGS		= -DLOCALSTATEDIR=\"${localstatedir}\" \
			  -DSOCKETDIR=\"${SOCKETDIR}\"

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= libcpg.a libconfdb.a libevs.a libcfg.a libquorum.a \
			  libvotequorum.a libpload.a libcoroipc.a
SHARED_LIBS		= $(lib_LIBRARIES:%.a=%.so.$(SONAME))
SHARED_LIBS_SO		= $(lib_LIBRARIES:%.a=%.so)
SHARED_LIBS_SO_TWO	= $(lib_LIBRARIES:%.a=%.so.$(SOMAJOR))

libcpg_a_SOURCES	= coroipc.c cpg.c
libcfg_a_SOURCES	= coroipc.c cfg.c
libevs_a_SOURCES	= coroipc.c evs.c
libpload_a_SOURCES	= coroipc.c pload.c
libquorum_a_SOURCES	= coroipc.c quorum.c
libvotequorum_a_SOURCES	= coroipc.c votequorum.c
libconfdb_a_SOURCES	= coroipc.c confdb.c sa-confdb.c
libcoroipc_a_SOURCES	= coroipc.c

noinst_HEADERS		= sa-confdb.h util.h \
			  libcfg.versions libconfdb.versions \
			  libcoroipc.versions libcpg.versions \
			  libevs.versions libpload.versions \
			  libquorum.versions libvotequorum.versions

if BUILD_DARWIN

libcoroipc.so.$(SONAME): coroipc.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipc.o -o $@

libconfdb.so.$(SONAME): coroipc.o confdb.o sa-confdb.o 
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipc.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o -o $@ -ldl

lib%.so.$(SONAME): coroipc.o %.o
	$(CC) $(DARWIN_OPTS) $^ -o $@

else

libcoroipc.so.$(SONAME): coroipc.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libcoroipc.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libcoroipc.versions \
		$^ $(LDFLAGS) $(AM_LDFLAGS)
	ln -sf libcoroipc.so.$(SONAME) libcoroipc.so
	ln -sf libcoroipc.so.$(SONAME) libcoroipc.so.$(SOMAJOR)

libconfdb.so.$(SONAME): coroipc.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libconfdb.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libconfdb.versions \
		$^ $(LDFLAGS) $(OS_DYFLAGS) -ldl
	ln -sf libconfdb.so.$(SONAME) libconfdb.so
	ln -sf libconfdb.so.$(SONAME) libconfdb.so.$(SOMAJOR)

lib%.so.$(SONAME): coroipc.o %.o
	$(CC) -shared -o $@ \
		-Wl,-soname=lib$*.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/lib$*.versions \
		$^ $(LDFLAGS)
	ln -sf lib$*.so.$(SONAME) lib$*.so
	ln -sf lib$*.so.$(SONAME) lib$*.so.$(SOMAJOR)

endif

all-local: $(SHARED_LIBS)
	@echo Built shared libs

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	cp -a $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir)/ && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg


