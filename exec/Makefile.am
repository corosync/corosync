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
AM_CPPFLAGS		= -DSYSCONFDIR=\"${sysconfdir}\" \
			  -DLOCALSTATEDIR=\"${localstatedir}\" \
			  -DLCRSODIR=\"${LCRSODIR}\" \
			  -DSOCKETDIR=\"${SOCKETDIR}\"

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

TOTEM_SRC		= coropoll.c totemip.c totemnet.c totemrrp.c \
			  totemsrp.c totemmrp.c totempg.c crypto.c wthread.c
LOGSYS_SRC		= wthread.c logsys.c

LCRSO_SRC		= objdb.c vsf_ykd.c coroparse.c quorum.c vsf_quorum.c
LCRSO_OBJS		= $(LCRSO_SRC:%.c=%.o)
LCRSO			= $(LCRSO_SRC:%.c=%.lcrso)

lib_LIBRARIES		= libtotem_pg.a liblogsys.a
sbin_PROGRAMS		= corosync

libtotem_pg_a_SOURCES	= $(TOTEM_SRC)
liblogsys_a_SOURCES	= $(LOGSYS_SRC)

corosync_SOURCES 	= main.c mempool.c util.c sync.c apidef.c service.c ipc.c \
			  timer.c totemconfig.c mainconfig.c quorum.c ../lcr/lcr_ifact.c
corosync_LDADD	  	= -ltotem_pg -llogsys
corosync_DEPENDENCIES	= libtotem_pg.so.$(SONAME) liblogsys.so.$(SONAME)
corosync_LDFLAGS	= $(OS_DYFLAGS) -L./

TOTEM_OBJS		= $(TOTEM_SRC:%.c=%.o)
LOGSYS_OBJS		= $(LOGSYS_SRC:%.c=%.o)

SHARED_LIBS		= $(lib_LIBRARIES:%.a=%.so.$(SONAME))
SHARED_LIBS_SO		= $(SHARED_LIBS:%.so.$(SONAME)=%.so)
SHARED_LIBS_SO_TWO	= $(SHARED_LIBS:%.so.$(SONAME)=%.so.$(SOMAJOR))

noinst_HEADERS		= apidef.h crypto.h ipc.h mainconfig.h main.h mempool.h \
			  quorum.h service.h sync.h timer.h tlist.h totemconfig.h \
			  totemmrp.h totemnet.h totemrrp.h totemsrp.h util.h \
			  version.h vsf.h wthread.h

EXTRA_DIST		= $(LCRSO_SRC)

if BUILD_DARWIN
%.lcrso: %.o
	$(CC) $(CFLAGS) -bundle -bind_at_load -bundle_loader ./corosync $^ -o $@

libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) $(TOTEM_OBJS) -o $@ -lpthread

liblogsys.so.$(SONAME): $(LOGSYS_OBJS)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) $(LOGSYS_OBJS) -o $@ -lpthread

else

%.lcrso: %.o
	$(CC) $(CFLAGS) -shared -Wl,-soname=$@ $^ -o $@

libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(CC) -shared -o $@ \
		-Wl,-soname=libtotem_pg.so.$(SOMAJOR) \
		$^ $(LDFLAGS) -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

liblogsys.so.$(SONAME): $(LOGSYS_OBJS)
	$(CC) -shared -o $@ \
		-Wl,-soname=liblogsys.so.$(SOMAJOR) \
		$^ $(LDFLAGS) -lpthread
	ln -sf liblogsys.so.$(SONAME) liblogsys.so
	ln -sf liblogsys.so.$(SONAME) liblogsys.so.$(SOMAJOR)

endif

lint:
	-splint $(LINT_FLAGS) $(CFLAGS) *.c

all-local: $(LCRSO_OBJS) $(LCRSO) $(SHARED_LIBS)
	@echo Built corosync Executive

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	cp -a $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)
	$(INSTALL) -d $(DESTDIR)/$(LCRSODIR)
	$(INSTALL) -m 755 $(LCRSO) $(DESTDIR)/$(LCRSODIR)

uninstall-local:
	cd $(DESTDIR)/$(libdir) && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)
	cd $(DESTDIR)/$(LCRSODIR) && \
		rm -f $(LCRSO)

clean-local:
	rm -f corosync *.o *.lcrso gmon.out *.da *.bb *.bbg *.so*
