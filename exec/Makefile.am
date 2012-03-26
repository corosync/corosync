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

MAINTAINERCLEANFILES	= Makefile.in

AM_CFLAGS		= -fPIC

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include $(nss_CFLAGS) $(rdmacm_CFLAGS) $(ibverbs_CFLAGS)

TOTEM_SRC		= coropoll.c totemip.c totemnet.c totemudp.c \
			  totemudpu.c totemrrp.c totemsrp.c totemmrp.c \
			  totempg.c crypto.c wthread.c
if BUILD_RDMA
TOTEM_SRC		+= totemiba.c
endif

LOGSYS_SRC		= wthread.c logsys.c
COROIPCS_SRC		= coroipcs.c

LCRSO_SRC		= objdb.c vsf_ykd.c coroparse.c vsf_quorum.c
LCRSO_OBJS		= $(LCRSO_SRC:%.c=%.o)
LCRSO			= $(LCRSO_SRC:%.c=%.lcrso)

lib_LIBRARIES		= libtotem_pg.a liblogsys.a libcoroipcs.a
sbin_PROGRAMS		= corosync

libtotem_pg_a_SOURCES	= $(TOTEM_SRC)
liblogsys_a_SOURCES	= $(LOGSYS_SRC)
libcoroipcs_a_SOURCES	= $(COROIPCS_SRC)

corosync_SOURCES 	= main.c util.c sync.c apidef.c service.c \
			  timer.c totemconfig.c mainconfig.c quorum.c schedwrk.c \
			  ../lcr/lcr_ifact.c evil.c syncv2.c
corosync_LDADD	  	= -ltotem_pg -llogsys -lcoroipcs
corosync_DEPENDENCIES	= libtotem_pg.so.$(SONAME) liblogsys.so.$(SONAME) libcoroipcs.so.$(SONAME)
corosync_LDFLAGS	= $(OS_DYFLAGS) -L./

TOTEM_OBJS		= $(TOTEM_SRC:%.c=%.o)
LOGSYS_OBJS		= $(LOGSYS_SRC:%.c=%.o)
COROIPCS_OBJS		= $(COROIPCS_SRC:%.c=%.o)

SHARED_LIBS		= $(lib_LIBRARIES:%.a=%.so.$(SONAME))
SHARED_LIBS_SO		= $(SHARED_LIBS:%.so.$(SONAME)=%.so)
SHARED_LIBS_SO_TWO	= $(SHARED_LIBS:%.so.$(SONAME)=%.so.$(SOMAJOR))

noinst_HEADERS		= apidef.h crypto.h mainconfig.h main.h \
			  quorum.h service.h sync.h timer.h tlist.h \
			  totemconfig.h totemmrp.h totemnet.h totemudp.h \
			  totemudpu.h totemiba.h totemrrp.h totemsrp.h util.h \
			  vsf.h wthread.h schedwrk.h evil.h syncv2.h

EXTRA_DIST		= $(LCRSO_SRC)

if BUILD_DARWIN
%.lcrso: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) -L$(top_builddir)/exec -llogsys -bundle -bind_at_load -bundle_loader ./corosync $^ -o $@

libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) $(TOTEM_OBJS) -o $@ -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

liblogsys.so.$(SONAME): $(LOGSYS_OBJS)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) $(LOGSYS_OBJS) -o $@ -lpthread
	ln -sf liblogsys.so.$(SONAME) liblogsys.so
	ln -sf liblogsys.so.$(SONAME) liblogsys.so.$(SOMAJOR)

libcoroipcs.so.$(SONAME): $(COROIPCS_OBJS)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) $(COROIPCS_OBJS) -o $@ -lpthread
	ln -sf libcoroipcs.so.$(SONAME) libcoroipcs.so
	ln -sf libcoroipcs.so.$(SONAME) libcoroipcs.so.$(SOMAJOR)

else

if BUILD_SOLARIS
%.lcrso: %.o
	$(LD) $(LDFLAGS) -G $^ -o $@

libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(LD) $(LDFLAGS) -G $(TOTEM_OBJS) -o $@ -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

liblogsys.so.$(SONAME): $(LOGSYS_OBJS)
	$(LD) $(LDFLAGS) -G $(LOGSYS_OBJS) -o $@ -lpthread
	ln -sf liblogsys.so.$(SONAME) liblogsys.so
	ln -sf liblogsys.so.$(SONAME) liblogsys.so.$(SOMAJOR)

libcoroipcs.so.$(SONAME): $(COROIPCS_OBJS)
	$(LD) $(LDFLAGS) -G $(COROIPCS_OBJS) -o $@ -lpthread
	ln -sf libcoroipcs.so.$(SONAME) libcoroipcs.so
	ln -sf libcoroipcs.so.$(SONAME) libcoroipcs.so.$(SOMAJOR)

else

%.lcrso: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) $(COVERAGE_LCRSO_EXTRA_LDFLAGS) -shared -Wl,-soname=$@ $^ -o $@

libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(CC) -shared -o $@ \
		-Wl,-soname=libtotem_pg.so.$(SOMAJOR) \
		$(LDFLAGS) $^ $(nss_LIBS) $(rdmacm_LIBS) $(ibverbs_LIBS) -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

liblogsys.so.$(SONAME): $(LOGSYS_OBJS)
	$(CC) -shared -o $@ \
		-Wl,-soname=liblogsys.so.$(SOMAJOR) \
		$(LDFLAGS) $^ -lpthread
	ln -sf liblogsys.so.$(SONAME) liblogsys.so
	ln -sf liblogsys.so.$(SONAME) liblogsys.so.$(SOMAJOR)

libcoroipcs.so.$(SONAME): $(COROIPCS_OBJS)
	$(CC) -shared -o $@ \
		-Wl,-soname=libcoroipcs.so.$(SOMAJOR) \
		$(LDFLAGS) $^ -lpthread
	ln -sf libcoroipcs.so.$(SONAME) libcoroipcs.so
	ln -sf libcoroipcs.so.$(SONAME) libcoroipcs.so.$(SOMAJOR)

endif

endif

lint:
	-splint $(INCLUDES) $(LINT_FLAGS) $(CFLAGS) *.c

all-local: $(LCRSO_OBJS) $(LCRSO) $(SHARED_LIBS)
	@echo Built corosync Executive

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	$(CP) $(CP_ARGS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)
	$(INSTALL) -d $(DESTDIR)/$(LCRSODIR)
	$(INSTALL) -m 755 $(LCRSO) $(DESTDIR)/$(LCRSODIR)

uninstall-local:
	cd $(DESTDIR)/$(libdir) && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)
	cd $(DESTDIR)/$(LCRSODIR) && \
		rm -f $(LCRSO)

clean-local:
	rm -f corosync *.o *.lcrso gmon.out *.da *.bb *.bbg *.so*
