# Copyright (c) 2002-2006 MontaVista Software, Inc.
# Copyright (c) 2006 Sun Microsystems, Inc.
# Copyright (c) 2006-2007 Red Hat, Inc.
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
LCR_SRC = evs.c clm.c ckpt.c evt.c lck.c msg.c cfg.c cpg.c aisparser.c vsf_ykd.c objdb.c $(AMF_SRC)
LCR_OBJS = evs.o clm.o ckpt.o evt.o lck.o msg.o cfg.o cpg.o aisparser.o vsf_ykd.o objdb.o $(AMF_OBJS)

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
	$(CC) $(LDFLAGS) $(EXEC_OBJS) $(EXEC_LIBS) -o aisexec

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
	$(CC) $(LDFLAGS) -shared -Wl,-soname,liblogsys.so.2 $(LOGSYS_OBJS) -o $@

endif

keygen: keygen.o
	$(CC) $(LDFLAGS) keygen.o -o keygen

openais-instantiate: openais-instantiate.o
	$(CC) $(LDFLAGS) openais-instantiate.o -o openais-instantiate

clean:
	rm -f aisexec $(OBJS) *.o *.lcrso libtotem_pg.so* libtotem_pg.a gmon.out keygen keygen.o openais-instantiate *.da *.bb *.bbg

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(EXEC_SRC) $(TOTEM_SRC) $(LOGSYS_SRC) $(LCR_SRC) > /dev/null 2>&1

# - fPIC rules required for service handler shared objects
../lcr/lcr_ifact.o: ../lcr/lcr_ifact.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DPREFIX='"$(PREFIX)"' -I../lcr -c -o $@ ../lcr/lcr_ifact.c

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

# DO NOT DELETE

aispoll.o: aispoll.h ../include/list.h ../include/hdb.h tlist.h
totemip.o: totemip.h ../include/swab.h
totemnet.o: aispoll.h totemnet.h totem.h totemip.h wthread.h
totemnet.o: ../include/queue.h ../include/sq.h ../include/list.h
totemnet.o: ../include/hdb.h ../include/swab.h crypto.h
totemrrp.o: ../include/queue.h ../include/sq.h ../include/list.h
totemrrp.o: ../include/hdb.h ../include/swab.h aispoll.h totemnet.h totem.h totemip.h
totemrrp.o: totemrrp.h
totemsrp.o: aispoll.h totemsrp.h totem.h totemip.h totemrrp.h wthread.h
totemsrp.o: ../include/queue.h ../include/sq.h ../include/list.h
totemsrp.o: ../include/hdb.h ../include/swab.h crypto.h
totemmrp.o: totem.h totemip.h totemsrp.h aispoll.h
totempg.o: ../include/hdb.h totempg.h aispoll.h totemsrp.h totem.h totemip.h
totempg.o: totemmrp.h ../include/swab.h
tlist.o: ../include/list.h tlist.h
crypto.o: crypto.h
wthread.o: wthread.h ../include/queue.h
evs.o: totem.h totemip.h ../include/saAis.h ../include/ipc_gen.h
evs.o: ../exec/totemip.h ../include/ipc_evs.h ../include/saAis.h
evs.o: ../include/evs.h ../include/ipc_gen.h ../include/list.h
evs.o: ../include/queue.h ../lcr/lcr_comp.h aispoll.h totempg.h totemsrp.h
evs.o: main.h ../include/saClm.h mainconfig.h objdb.h mempool.h service.h
evs.o: logsys.h
clm.o: totem.h totemip.h ../include/saAis.h ../include/saClm.h
clm.o: ../include/saAis.h ../include/ipc_gen.h ../exec/totemip.h
clm.o: ../include/ipc_clm.h ../include/saClm.h ../include/ipc_gen.h
clm.o: ../include/mar_clm.h ../include/mar_gen.h ../include/mar_gen.h
clm.o: ../include/mar_clm.h ../include/list.h ../include/queue.h
clm.o: ../lcr/lcr_comp.h aispoll.h totempg.h totemsrp.h main.h mainconfig.h
clm.o: objdb.h mempool.h service.h ../include/swab.h logsys.h
ckpt.o: ../include/saAis.h ../include/saCkpt.h ../include/ipc_ckpt.h
ckpt.o: ../include/saAis.h ../include/saCkpt.h ../include/ipc_gen.h
ckpt.o: ../include/list.h ../include/queue.h ../include/hdb.h
ckpt.o: ../lcr/lcr_comp.h aispoll.h service.h totem.h totemip.h objdb.h
ckpt.o: mempool.h util.h main.h ../include/saClm.h ../include/ipc_gen.h
ckpt.o: ../exec/totemip.h mainconfig.h totemsrp.h totempg.h logsys.h
evt.o: ../include/hdb.h ../include/ipc_evt.h ../include/saAis.h
evt.o: ../include/saEvt.h ../include/saClm.h ../include/ipc_gen.h
evt.o: ../include/list.h ../include/queue.h ../lcr/lcr_comp.h util.h
evt.o: ../include/saAis.h service.h totem.h totemip.h objdb.h aispoll.h
evt.o: mempool.h main.h ../include/saClm.h ../include/ipc_gen.h
evt.o: ../exec/totemip.h mainconfig.h totemsrp.h totempg.h ../include/swab.h logsys.h
lck.o: service.h totem.h totemip.h objdb.h ../include/saAis.h
lck.o: ../include/saLck.h ../include/ipc_lck.h ../include/saAis.h
lck.o: ../include/saLck.h ../include/ipc_gen.h ../include/list.h
lck.o: ../include/queue.h ../lcr/lcr_comp.h aispoll.h mempool.h util.h main.h
lck.o: ../include/saClm.h ../include/ipc_gen.h ../exec/totemip.h mainconfig.h
lck.o: totemsrp.h totempg.h logsys.h
msg.o: ../include/saAis.h ../include/saMsg.h ../include/ipc_msg.h
msg.o: ../include/saAis.h ../include/saMsg.h ../include/ipc_gen.h
msg.o: ../include/list.h ../include/queue.h ../lcr/lcr_comp.h service.h
msg.o: totem.h totemip.h objdb.h aispoll.h mempool.h util.h main.h
msg.o: ../include/saClm.h ../include/ipc_gen.h ../exec/totemip.h mainconfig.h
msg.o: totemsrp.h totempg.h logsys.h
cfg.o: ../include/saAis.h ../include/cfg.h ../include/saAis.h
cfg.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_cfg.h
cfg.o: ../include/ipc_gen.h ../include/cfg.h ../include/list.h
cfg.o: ../include/queue.h ../lcr/lcr_comp.h service.h totem.h totemip.h
cfg.o: objdb.h totempg.h aispoll.h totemsrp.h mempool.h util.h logsys.h
cfg.o: mainconfig.h
cpg.o: ../include/saAis.h ../include/saClm.h ../include/saAis.h
cpg.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_cpg.h
cpg.o: ../include/ipc_gen.h ../include/list.h ../include/queue.h
cpg.o: ../lcr/lcr_comp.h aispoll.h totempg.h totemsrp.h totem.h totemip.h
cpg.o: main.h mainconfig.h objdb.h mempool.h service.h jhash.h ../include/swab.h logsys.h
aisparser.o: ../lcr/lcr_comp.h objdb.h config.h mempool.h ../include/list.h
aisparser.o: util.h ../include/saAis.h
vsf_ykd.o: main.h ../include/saAis.h ../include/saClm.h ../include/saAis.h
vsf_ykd.o: ../include/queue.h ../include/ipc_gen.h ../exec/totemip.h
vsf_ykd.o: mainconfig.h ../include/list.h aispoll.h totemsrp.h totem.h
vsf_ykd.o: totemip.h totempg.h objdb.h logsys.h ../include/swab.h vsf.h ../lcr/lcr_comp.h
amf.o: ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amf.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_amf.h
amf.o: ../include/ipc_gen.h ../include/saAmf.h ../include/list.h
amf.o: ../lcr/lcr_comp.h totempg.h aispoll.h totemsrp.h totem.h totemip.h
amf.o: mempool.h util.h amf.h objdb.h main.h ../include/saClm.h
amf.o: ../include/queue.h mainconfig.h service.h logsys.h
amfutil.o: ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amfutil.o: ../include/ipc_amf.h ../include/ipc_gen.h ../include/saAmf.h
amfutil.o: ../include/list.h util.h amf.h ../include/ipc_gen.h
amfutil.o: ../exec/totemip.h aispoll.h objdb.h totem.h totemip.h logsys.h
amfutil.o: mainconfig.h totemsrp.h totempg.h
amfcluster.o: logsys.h mainconfig.h ../include/saAis.h ../include/list.h
amfcluster.o: aispoll.h totemsrp.h totem.h totemip.h totempg.h objdb.h amf.h
amfcluster.o: ../include/saAmf.h ../include/saAis.h ../include/ipc_gen.h
amfcluster.o: ../exec/totemip.h util.h main.h ../include/saClm.h
amfcluster.o: ../include/queue.h
amfapp.o: amf.h ../include/saAis.h ../include/saAmf.h
amfapp.o: ../include/saAis.h ../include/list.h ../include/ipc_gen.h
amfapp.o: ../exec/totemip.h aispoll.h objdb.h logsys.h mainconfig.h
amfapp.o: totemsrp.h totem.h totemip.h totempg.h
amfsg.o: amf.h ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amfsg.o: ../include/list.h ../include/ipc_gen.h ../exec/totemip.h aispoll.h
amfsg.o: objdb.h logsys.h mainconfig.h totemsrp.h totem.h totemip.h totempg.h
amfsg.o: main.h ../include/saClm.h ../include/queue.h
amfsu.o: amf.h ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amfsu.o: ../include/list.h ../include/ipc_gen.h ../exec/totemip.h aispoll.h
amfsu.o: objdb.h util.h logsys.h mainconfig.h totemsrp.h totem.h totemip.h
amfsu.o: totempg.h main.h ../include/saClm.h ../include/queue.h
amfcomp.o: ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amfcomp.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_amf.h
amfcomp.o: ../include/ipc_gen.h ../include/saAmf.h totempg.h aispoll.h
amfcomp.o: totemsrp.h totem.h totemip.h main.h ../include/saClm.h
amfcomp.o: ../include/queue.h mainconfig.h ../include/list.h objdb.h
amfcomp.o: service.h util.h amf.h logsys.h
amfsi.o: amf.h ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amfsi.o: ../include/list.h ../include/ipc_gen.h ../exec/totemip.h aispoll.h
amfsi.o: objdb.h logsys.h mainconfig.h totemsrp.h totem.h totemip.h totempg.h
