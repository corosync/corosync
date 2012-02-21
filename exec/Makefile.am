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

TOTEM_SRC		= totemip.c totemnet.c totemudp.c \
			  totemudpu.c totemrrp.c totemsrp.c totemmrp.c \
			  totempg.c crypto.c cs_queue.h
if BUILD_RDMA
TOTEM_SRC		+= totemiba.c
endif

lib_LIBRARIES		= libtotem_pg.a
sbin_PROGRAMS		= corosync

libtotem_pg_a_SOURCES	= $(TOTEM_SRC)

corosync_SOURCES	= evil.c vsf_ykd.c coroparse.c vsf_quorum.c syncv2.c \
			  logsys.c cfg.c cmap.c cpg.c evs.c mon.c pload.c \
			  votequorum.c wd.c util.c schedwrk.c main.c \
			  apidef.c quorum.c sync.c icmap.c timer.c \
			  ipc_glue.c service.c mainconfig.c totemconfig.c
corosync_LDADD		= -ltotem_pg -lcorosync_common $(LIBQB_LIBS) $(statgrab_LIBS)
corosync_DEPENDENCIES	= libtotem_pg.so.$(SONAME) ../common_lib/libcorosync_common.so
corosync_LDFLAGS	= $(OS_DYFLAGS) -L./ -L../common_lib

TOTEM_OBJS		= $(TOTEM_SRC:%.c=%.o)
LOGSYS_OBJS		= $(LOGSYS_SRC:%.c=%.o)

SHARED_LIBS		= $(lib_LIBRARIES:%.a=%.so.$(SONAME))
SHARED_LIBS_SO		= $(SHARED_LIBS:%.so.$(SONAME)=%.so)
SHARED_LIBS_SO_TWO	= $(SHARED_LIBS:%.so.$(SONAME)=%.so.$(SOMAJOR))

noinst_HEADERS		= apidef.h crypto.h mainconfig.h main.h \
			  quorum.h service.h sync.h timer.h totemconfig.h \
			  totemmrp.h totemnet.h totemudp.h totemiba.h totemrrp.h \
			  totemudpu.h totemsrp.h util.h vsf.h schedwrk.h \
			  evil.h syncv2.h fsm.h votequorum.h vsf_ykd.h

if BUILD_DARWIN
libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) $(TOTEM_OBJS) -o $@ -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

else

if BUILD_SOLARIS
libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(LD) $(LDFLAGS) -G $(TOTEM_OBJS) -o $@ -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

else
libtotem_pg.so.$(SONAME): $(TOTEM_OBJS)
	$(CC) -shared -o $@ \
		-Wl,-soname=libtotem_pg.so.$(SOMAJOR) \
		$(LDFLAGS) $^ $(nss_LIBS) $(rdmacm_LIBS) $(ibverbs_LIBS) -lpthread
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so
	ln -sf libtotem_pg.so.$(SONAME) libtotem_pg.so.$(SOMAJOR)

endif

endif

lint:
	-splint $(INCLUDES) $(LINT_FLAGS) $(CFLAGS) *.c

all-local: $(LCRSO_OBJS) $(LCRSO) $(SHARED_LIBS)
	@echo Built corosync Executive

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	$(CP) -a $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir) && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)
	cd $(DESTDIR)/$(LCRSODIR) && \
		rm -f $(LCRSO)

clean-local:
	rm -f corosync *.o gmon.out *.da *.bb *.bbg *.so*
