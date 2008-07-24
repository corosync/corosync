# Copyright (c) 2002-2006 MontaVista Software, Inc.
# Copyright (c) 2006-2008 Red Hat, Inc.
# 
# All rights reserved.
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

# Include configuration
#
srcdir ?= $(CURDIR)/../
subdir ?= exec/

include $(srcdir)Makefile.inc

LDFLAGS += -L./

ifeq (${BUILD_DYNAMIC}, 1) 
	override LDFLAGS += ${DYFLAGS}
endif

ifeq (${OPENAIS_COMPAT}, LINUX) 
	override LDFLAGS += -ldl
endif

ifeq (${OPENAIS_COMPAT}, SOLARIS) 
	override LDFLAGS += -lnsl -lsocket -lrt
endif

# Totem objects
TOTEM_SRC = coropoll.c totemip.c totemnet.c totemrrp.c totemsrp.c totemmrp.c totempg.c crypto.c wthread.c
TOTEM_OBJS = coropoll.o totemip.o totemnet.o totemrrp.o totemsrp.o totemmrp.o totempg.o crypto.o wthread.o
LOGSYS_SRC = wthread.c logsys.c
LOGSYS_OBJS = wthread.o logsys.o

EXEC_LIBS = libtotem_pg.a liblogsys.a

# LCR objects
LCR_SRC = vsf_ykd.c objdb.c coroparse.c
LCR_OBJS =aisparser.o vsf_ykd.o objdb.o coroparse.o

# main executive objects
MAIN_SRC = main.c mempool.c util.c sync.c apidef.c service.c ipc.c flow.c \
	timer.c totemconfig.c mainconfig.c
MAIN_OBJS = main.o mempool.o util.o sync.o apidef.o service.o ipc.o flow.o \
	timer.o totemconfig.o mainconfig.o ../lcr/lcr_ifact.o

ifeq (${BUILD_DYNAMIC}, 1) 
#EXEC_OBJS = $(TOTEM_OBJS) $(LOGSYS_OBJS) $(MAIN_OBJS)
EXEC_OBJS = $(MAIN_OBJS)
override CFLAGS += -fPIC

all:libtotem_pg.a libtotem_pg.so.2.0.0 liblogsys.a liblogsys.so.2.0.0 \
	../lcr/lcr_ifact.o corosync \
	objdb.lcrso vsf_ykd.lcrso coroparse.lcrso keygen
else
EXEC_OBJS = $(MAIN_OBJS) $(LCR_OBJS)
all: libtotem_pg.a liblogsys.a corosync keygen
endif


ifeq (${OPENAIS_COMPAT}, DARWIN)

objdb.lcrso: objdb.o
	$(CC) -bundle -bundle_loader ./corosync -bind_at_load objdb.o -o $@

vsf_ykd.lcrso: vsf_ykd.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./corosync -bind_at_load vsf_ykd.o -o $@

coroparse.lcrso: coroparse.o
	$(CC) -bundle -bundle_loader ./corosync -bind_at_load coroparse.o -o $@

else

vsf_ykd.lcrso: vsf_ykd.o
	$(CC) -shared -Wl,-soname,vsf_ykd.lcrso vsf_ykd.o -o $@

objdb.lcrso: objdb.o
	$(CC) -shared -Wl,-soname,objdb.lcrso objdb.o -o $@

coroparse.lcrso: coroparse.o
	$(CC) -shared -Wl,-soname,coroparse.lcrso coroparse.o -o $@
endif

corosync: $(EXEC_OBJS) $(EXEC_LIBS)
	$(CC) $(EXEC_OBJS) $(EXEC_LIBS) -o corosync $(LDFLAGS)

libtotem_pg.a: $(TOTEM_OBJS)
	$(AR) -rc libtotem_pg.a $(TOTEM_OBJS)

liblogsys.a: $(LOGSYS_OBJS)
	$(AR) -rc liblogsys.a $(LOGSYS_OBJS)

ifeq (${OPENAIS_COMPAT}, DARWIN)

libtotem_pg.so.2.0.0: $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) -bundle -bind_at_load $(TOTEM_OBJS) -o $@

liblogsys.so.2.0.0: $(LOGSYS_OBJS)
	$(CC) $(LDFLAGS) -bundle -bind_at_load $(LOGSYS_OBJS) -o $@
else

libtotem_pg.so.2.0.0: $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libtotem_pg.so.2 $(TOTEM_OBJS) -o $@

liblogsys.so.2.0.0: $(LIBLOGSYS_OBJS)
	$(CC) -shared -Wl,-soname,liblogsys.so.2 $(LOGSYS_OBJS) -o $@ $(LDFLAGS)

endif

keygen: keygen.o
	$(CC) $(LDFLAGS) keygen.o -o keygen

openais-instantiate: openais-instantiate.o
	$(CC) $(LDFLAGS) openais-instantiate.o -o openais-instantiate

clean:
	rm -f corosync $(OBJS) *.o *.lcrso libtotem_pg.so* libtotem_pg.a gmon.out 
	rm -f keygen keygen.o openais-instantiate *.da *.bb *.bbg liblogsys.so* liblogsys.a

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(EXEC_SRC) $(TOTEM_SRC) $(LOGSYS_SRC) $(LCR_SRC) > /dev/null 2>&1

# - fPIC rules required for service handler shared objects
../lcr/lcr_ifact.o: $(srcdir)lcr/lcr_ifact.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DPREFIX='"$(PREFIX)"' -DLCRSODIR='"$(LCRSODIR)"' -I../lcr -c -o $@ $<

vsf_ykd.o: vsf_ykd.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

objdb.o: objdb.c
	$(CC) $(CFLAGS) -c -o $@ $<

coroparse.o: coroparse.c
	$(CC) $(CFLAGS) -c -o $@ $<

# -fPIC rules required for lib totem
coropoll.o: coropoll.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totempg.o: totempg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totemsrp.o: totemsrp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totemrrp.o: totemrrp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totemip.o: totemip.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totemnet.o: totemnet.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

wthread.o: wthread.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

crypto.o: crypto.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totemmrp.o: totemmrp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

totemconfig.o: totemconfig.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

