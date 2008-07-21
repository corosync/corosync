# Copyright (c) 2002-2006 MontaVista Software, Inc.
# Copyright (c) 2006 Sun Microsystems, Inc.
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

include $(srcdir)Makefile.inc

override CFLAGS += -I$(srcdir)include
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
TOTEM_SRC = aispoll.c totemip.c totemnet.c totemrrp.c totemsrp.c totemmrp.c totempg.c crypto.c wthread.c
TOTEM_OBJS = aispoll.o totemip.o totemnet.o totemrrp.o totemsrp.o totemmrp.o totempg.o crypto.o wthread.o
LOGSYS_SRC = wthread.c logsys.c
LOGSYS_OBJS = wthread.o logsys.o

EXEC_LIBS = libtotem_pg.a liblogsys.a

# AMF objects
AMF_SRC = amf.c amfutil.c amfnode.c amfcluster.c amfapp.c amfsg.c amfsu.c amfcomp.c amfsi.c
AMF_OBJS = amf.o amfutil.o amfnode.o amfcluster.o amfapp.o amfsg.o amfsu.o amfcomp.o amfsi.o

# LCR objects
LCR_SRC = evs.c clm.c ckpt.c evt.c lck.c msg.c cfg.c cpg.c aisparser.c vsf_ykd.c objdb.c confdb.c $(AMF_SRC)
LCR_OBJS = evs.o clm.o ckpt.o evt.o lck.o msg.o cfg.o cpg.o aisparser.o vsf_ykd.o objdb.o confdb.o $(AMF_OBJS)

# main executive objects
MAIN_SRC = main.c mempool.c util.c sync.c service.c ipc.c flow.c timer.c \
	totemconfig.c mainconfig.c
MAIN_OBJS = main.o mempool.o util.o sync.o service.o ipc.o flow.o timer.o \
	totemconfig.o mainconfig.o ../lcr/lcr_ifact.o

ifeq (${BUILD_DYNAMIC}, 1) 
#EXEC_OBJS = $(TOTEM_OBJS) $(LOGSYS_OBJS) $(MAIN_OBJS)
EXEC_OBJS = $(MAIN_OBJS)
override CFLAGS += -fPIC

all:libtotem_pg.a libtotem_pg.so.2.0.0 liblogsys.a liblogsys.so.2.0.0 \
	../lcr/lcr_ifact.o aisexec \
	service_evs.lcrso service_clm.lcrso service_amf.lcrso \
	service_ckpt.lcrso service_evt.lcrso service_lck.lcrso \
	service_msg.lcrso service_cfg.lcrso service_cpg.lcrso \
	service_confdb.lcrso \
	objdb.lcrso aisparser.lcrso vsf_ykd.lcrso keygen openais-instantiate
else
EXEC_OBJS = $(MAIN_OBJS) $(LCR_OBJS)
all: libtotem_pg.a liblogsys.a aisexec keygen openais-instantiate
endif


ifeq (${OPENAIS_COMPAT}, DARWIN)

service_evs.lcrso: evs.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load evs.o -o $@

service_clm.lcrso: clm.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load clm.o -o $@

service_amf.lcrso: $(AMF_OBJS)
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load $(AMF_OBJS) -o $@

service_ckpt.lcrso: ckpt.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load ckpt.o -o $@

service_evt.lcrso: evt.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load evt.o -o $@

service_lck.lcrso: lck.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load lck.o -o $@

service_msg.lcrso: msg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load msg.o -o $@

service_cfg.lcrso: cfg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load cfg.o -o $@

service_confdb.lcrso: confdb.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load confdb.o -o $@

service_cpg.lcrso: cpg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load cpg.o -o $@

aisparser.lcrso: aisparser.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load aisparser.o -o $@

objdb.lcrso: objdb.o
	$(CC) -bundle -bundle_loader ./aisexec -bind_at_load objdb.o -o $@

vsf_ykd.lcrso: vsf_ykd.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load vsf_ykd.o -o $@


else

service_evs.lcrso: evs.o
	$(CC) -shared -Wl,-soname,service_evs.lcrso evs.o -o $@

service_clm.lcrso: clm.o
	$(CC) -shared -Wl,-soname,service_clm.lcrso clm.o -o $@

service_amf.lcrso: $(AMF_OBJS)
	$(CC) -shared -Wl,-soname,service_amf.lcrso $(AMF_OBJS) -o $@

service_ckpt.lcrso: ckpt.o
	$(CC) -shared -Wl,-soname,service_ckpt.lcrso ckpt.o -o $@

service_evt.lcrso: evt.o
	$(CC) -shared -Wl,-soname,service_evt.lcrso evt.o -o $@

service_lck.lcrso: lck.o
	$(CC) -shared -Wl,-soname,service_lck.lcrso lck.o -o $@

service_msg.lcrso: msg.o
	$(CC) -shared -Wl,-soname,service_msg.lcrso msg.o -o $@

service_cfg.lcrso: cfg.o
	$(CC) -shared -Wl,-soname,service_cfg.lcrso cfg.o -o $@

service_confdb.lcrso: confdb.o
	$(CC) -shared -Wl,-soname,service_confdb.lcrso confdb.o -o $@

service_cpg.lcrso: cpg.o
	$(CC) -shared -Wl,-soname,service_cpg.lcrso cpg.o -o $@

aisparser.lcrso: aisparser.o
	$(CC) -shared -Wl,-soname,aisparser.lcrso aisparser.o -o $@

vsf_ykd.lcrso: vsf_ykd.o
	$(CC) -shared -Wl,-soname,vsf_ykd.lcrso vsf_ykd.o -o $@

objdb.lcrso: objdb.o
	$(CC) -shared -Wl,-soname,objdb.lcrso objdb.o -o $@

endif

aisexec: $(EXEC_OBJS) $(EXEC_LIBS)
	$(CC) $(EXEC_OBJS) $(EXEC_LIBS) -o aisexec $(LDFLAGS)

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
	rm -f aisexec $(OBJS) *.o *.lcrso libtotem_pg.so* libtotem_pg.a gmon.out 
	rm -f keygen keygen.o openais-instantiate *.da *.bb *.bbg liblogsys.so* liblogsys.a

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(EXEC_SRC) $(TOTEM_SRC) $(LOGSYS_SRC) $(LCR_SRC) > /dev/null 2>&1

# - fPIC rules required for service handler shared objects
../lcr/lcr_ifact.o: ../lcr/lcr_ifact.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DPREFIX='"$(PREFIX)"' -DLCRSODIR='"$(LCRSODIR)"' -I../lcr -c -o $@ ../lcr/lcr_ifact.c

evs.o: evs.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clm.o: clm.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

amf.o: amf.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

ckpt.o: ckpt.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

evt.o: evt.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

lck.o: lck.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

msg.o: msg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

cfg.o: cfg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

confdb.o: confdb.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

aisparser.o: aisparser.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

vsf_ykd.o: vsf_ykd.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

cpg.o: cpg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

objdb.o: objdb.c
	$(CC) $(CFLAGS) -c -o $@ $<

# -fPIC rules required for lib totem
aispoll.o: aispoll.c
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

