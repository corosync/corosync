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

AM_LDFLAGS		= -lpthread

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= libcpg.a libconfdb.a libevs.a libcfg.a libquorum.a \
			  libvotequorum.a libpload.a libcoroipcc.a libsam.a
SHARED_LIBS		= $(filter-out libcpg.so.$(SONAME), $(lib_LIBRARIES:%.a=%.so.$(SONAME))) libcpg.so.$(CPG_SONAME)
SHARED_LIBS_SO		= $(lib_LIBRARIES:%.a=%.so)
SHARED_LIBS_SO_TWO	= $(lib_LIBRARIES:%.a=%.so.$(SOMAJOR))

libcpg_a_SOURCES	= cpg.c
libcfg_a_SOURCES	= cfg.c
libevs_a_SOURCES	= evs.c
libpload_a_SOURCES	= pload.c
libquorum_a_SOURCES	= quorum.c
libvotequorum_a_SOURCES	= votequorum.c
libconfdb_a_SOURCES	= confdb.c sa-confdb.c
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

libconfdb.so.$(SONAME): confdb.o sa-confdb.o libcoroipcc.so.$(SONAME)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipcc.o confdb.o \
		sa-confdb.o ../lcr/lcr_ifact.o -o $@ -ldl $(AM_LDFLAGS)
	ln -sf libconfdb.so.$(SONAME) libconfdb.so
	ln -sf libconfdb.so.$(SONAME) libconfdb.so.$(SOMAJOR)

libcpg.so.$(CPG_SONAME): cpg.o libcoroipcc.so.$(SONAME)
	$(CC) $(DARWIN_OPTS) $^ -o $@
	ln -sf $@ libcpg.so
	ln -sf $@ libcpg.so.$(SOMAJOR)

lib%.so.$(SONAME): %.o libcoroipcc.so.$(SONAME)
	$(CC) $(DARWIN_OPTS) $^ -o $@
	ln -sf lib$*.so.$(SONAME) lib$*.so
	ln -sf lib$*.so.$(SONAME) lib$*.so.$(SOMAJOR)

else

if BUILD_SOLARIS

libcoroipcc.so.$(SONAME): coroipcc.o
	$(LD) $(LDFLAGS) $(SOLARIS_OPTS) -G coroipcc.o -o $@ $(AM_LDFLAGS) -lsocket -lnsl
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so.$(SOMAJOR)

libconfdb.so.$(SONAME): confdb.o sa-confdb.o libcoroipcc.so.$(SONAME)
	$(LD) $(LDFLAGS) $(SOLARIS_OPTS) -G coroipcc.o confdb.o \
		sa-confdb.o ../lcr/lcr_ifact.o -o $@ -ldl $(AM_LDFLAGS)
	ln -sf libconfdb.so.$(SONAME) libconfdb.so
	ln -sf libconfdb.so.$(SONAME) libconfdb.so.$(SOMAJOR)

libcpg.so.$(CPG_SONAME): cpg.o libcoroipcc.so.$(SONAME)
	$(LD) $(SOLARIS_OPTS) -G $^ -o $@
	ln -sf $@ libcpg.so
	ln -sf $@ libcpg.so.$(SOMAJOR)

lib%.so.$(SONAME): %.o libcoroipcc.so.$(SONAME)
	$(LD) $(SOLARIS_OPTS) -G $^ -o $@
	ln -sf lib$*.so.$(SONAME) lib$*.so
	ln -sf lib$*.so.$(SONAME) lib$*.so.$(SOMAJOR)

else

libcoroipcc.so.$(SONAME): coroipcc.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libcoroipcc.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libcoroipcc.versions \
		$^ $(LDFLAGS) $(AM_LDFLAGS)
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so
	ln -sf libcoroipcc.so.$(SONAME) libcoroipcc.so.$(SOMAJOR)

libconfdb.so.$(SONAME): confdb.o sa-confdb.o ../lcr/lcr_ifact.o libcoroipcc.so.$(SONAME)
	$(CC) -shared -o $@ \
		-Wl,-soname=libconfdb.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libconfdb.versions \
		$^ $(LDFLAGS) $(OS_DYFLAGS) $(OS_LDL) $(AM_LDFLAGS)
	ln -sf libconfdb.so.$(SONAME) libconfdb.so
	ln -sf libconfdb.so.$(SONAME) libconfdb.so.$(SOMAJOR)

libcpg.so.$(CPG_SONAME): cpg.o libcoroipcc.so.$(SONAME)
	$(CC) -shared -o $@ \
		-Wl,-soname=libcpg.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libcpg.versions \
		$^ $(LDFLAGS) $(AM_LDFLAGS)
	ln -sf $@ libcpg.so
	ln -sf $@ libcpg.so.$(SOMAJOR)

lib%.so.$(SONAME): %.o libcoroipcc.so.$(SONAME)
	$(CC) -shared -o $@ \
		-Wl,-soname=lib$*.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/lib$*.versions \
		$^ $(LDFLAGS) $(AM_LDFLAGS)
	ln -sf lib$*.so.$(SONAME) lib$*.so
	ln -sf lib$*.so.$(SONAME) lib$*.so.$(SOMAJOR)

endif

endif

all-local: $(SHARED_LIBS)
	@echo Built shared libs

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	$(CP) -a $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir)/ && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg
