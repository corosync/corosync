# Copyright (c) 2002-2006 MontaVista Software, Inc.
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
include ../Makefile.inc

override CFLAGS += -I../include
LDFLAGS += -L./

ifeq (${BUILD_DYNAMIC}, 1) 
	override LDFLAGS += ${DYFLAGS}
endif

ifeq (${OPENAIS_COMPAT}, LINUX) 
	override LDFLAGS += -ldl
endif

# Totem objects
TOTEM_SRC = aispoll.c totemip.c totemnet.c totemrrp.c totemsrp.c totemmrp.c totempg.c crypto.c wthread.c
TOTEM_OBJS = aispoll.o totemip.o totemnet.o totemrrp.o totemsrp.o totemmrp.o totempg.o crypto.o wthread.o
EXEC_LIBS = libtotem_pg.a

# LCR objects
LCR_SRC = evs.c clm.c amf.c ckpt.c evt.c lck.c msg.c cfg.c cpg.c amfconfig.c aisparser.c vsf_ykd.c
LCR_OBJS = evs.o clm.o amf.o ckpt.o evt.o lck.o msg.o cfg.o cpg.o amfconfig.o aisparser.o vsf_ykd.o

# main executive objects
MAIN_SRC = main.c print.c mempool.c util.c sync.c service.c ipc.c timer.c \
	totemconfig.c mainconfig.c
MAIN_OBJS = main.o print.o mempool.o util.o sync.o service.o ipc.o timer.o \
	totemconfig.o mainconfig.o ../lcr/lcr_ifact.o
OTHER_OBJS = objdb.o

ifeq (${BUILD_DYNAMIC}, 1) 
EXEC_OBJS = $(TOTEM_OBJS) $(MAIN_OBJS)
override CFLAGS += -fPIC

all:libtotem_pg.a libtotem_pg.so.1.0.0 ../lcr/lcr_ifact.o \
	aisexec \
	service_evs.lcrso service_clm.lcrso service_amf.lcrso \
	service_ckpt.lcrso service_evt.lcrso service_lck.lcrso \
	service_msg.lcrso service_cfg.lcrso service_cpg.lcrso \
	objdb.lcrso aisparser.lcrso vsf_ykd.lcrso keygen openais-instantiate
else
EXEC_OBJS = $(TOTEM_OBJS) $(MAIN_OBJS) $(OTHER_OBJS) $(LCR_OBJS)
all: libtotem_pg.a aisexec keygen openais-instantiate
endif


ifeq (${OPENAIS_COMPAT}, DARWIN)

service_evs.lcrso: evs.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load evs.o -o $@

service_clm.lcrso: clm.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load clm.o -o $@

service_amf.lcrso: amf.o amfconfig.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load amf.o amfconfig.o -o $@

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

service_amf.lcrso: amf.o amfconfig.o
	$(CC) -shared -Wl,-soname,service_amf.lcrso amf.o amfconfig.o -o $@

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

aisexec: $(EXEC_OBJS) libtotem_pg.a
	$(CC) $(LDFLAGS) $(EXEC_OBJS) $(EXEC_LIBS) -o aisexec

libtotem_pg.a: $(TOTEM_OBJS)
	$(AR) -rc libtotem_pg.a $(TOTEM_OBJS)

ifeq (${OPENAIS_COMPAT}, DARWIN)

libtotem_pg.so.1.0.0: $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) -bundle -bind_at_load $(TOTEM_OBJS) -o $@

else

libtotem_pg.so.1.0.0: $(TOTEM_OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libtotem_pg.so.1 $(TOTEM_OBJS) -o $@

endif

keygen: keygen.o
	$(CC) $(LDFLAGS) keygen.o -o keygen

openais-instantiate: openais-instantiate.o
	$(CC) $(LDFLAGS) openais-instantiate.o -o openais-instantiate

clean:
	rm -f aisexec $(OBJS) *.o *.lcrso libtotem_pg.so* libtotem_pg.a gmon.out keygen keygen.o openais-instantiate *.da *.bb *.bbg

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(EXEC_SRC) $(TOTEM_SRC) $(LCR_SRC) > /dev/null 2>&1

# - fPIC rules required for service handler shared objects
../lcr/lcr_ifact.o: ../lcr/lcr_ifact.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -I../lcr -c -o $@ ../lcr/lcr_ifact.c

evs.o: evs.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

clm.o: clm.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

amf.o: amf.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

ckpt.o: ckpt.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

evt.o: evt.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

lck.o: lck.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

msg.o: msg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

cfg.o: cfg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

aisparser.o: aisparser.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

vsf_ykd.o: vsf_ykd.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

cpg.o: cpg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $(*F).c

objdb.o: objdb.c
	$(CC) $(CFLAGS) -c -o $@ $(*F).c

# -fPIC rules required for lib totem
aispoll.o: aispoll.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totempg.o: totempg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totemsrp.o: totemsrp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totemrrp.o: totemrrp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totemip.o: totemip.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totemnet.o: totemnet.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

wthread.o: wthread.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

crypto.o: crypto.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totemmrp.o: totemmrp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

totemconfig.o: totemconfig.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

# DO NOT DELETE

aispoll.o: aispoll.h ../include/list.h ../include/hdb.h tlist.h
totemip.o: totemip.h swab.h
totemnet.o: aispoll.h totemnet.h totem.h totemip.h wthread.h
totemnet.o: ../include/queue.h ../include/sq.h ../include/list.h
totemnet.o: ../include/hdb.h swab.h crypto.h
totemrrp.o: ../include/queue.h ../include/sq.h ../include/list.h
totemrrp.o: ../include/hdb.h swab.h aispoll.h totemnet.h totem.h totemip.h
totemrrp.o: totemrrp.h
totemsrp.o: aispoll.h totemsrp.h totem.h totemip.h totemrrp.h wthread.h
totemsrp.o: ../include/queue.h ../include/sq.h ../include/list.h
totemsrp.o: ../include/hdb.h swab.h crypto.h
totemmrp.o: totem.h totemip.h totemsrp.h aispoll.h
totempg.o: ../include/hdb.h totempg.h aispoll.h totemsrp.h totem.h totemip.h
totempg.o: totemmrp.h swab.h
tlist.o: ../include/list.h tlist.h
crypto.o: crypto.h
wthread.o: wthread.h ../include/queue.h
evs.o: ../include/saAis.h ../include/ipc_gen.h ../exec/totemip.h
evs.o: ../include/ipc_evs.h ../include/saAis.h ../include/evs.h
evs.o: ../include/ipc_gen.h ../include/list.h ../include/queue.h
evs.o: ../lcr/lcr_comp.h aispoll.h totempg.h totemsrp.h totem.h totemip.h
evs.o: main.h ../include/saClm.h mainconfig.h objdb.h mempool.h service.h
evs.o: print.h
clm.o: ../include/saAis.h ../include/saClm.h ../include/saAis.h
clm.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_clm.h
clm.o: ../include/saClm.h ../include/ipc_gen.h ../include/list.h
clm.o: ../include/queue.h ../lcr/lcr_comp.h aispoll.h totempg.h totemsrp.h
clm.o: totem.h totemip.h main.h mainconfig.h objdb.h mempool.h service.h
clm.o: print.h
amf.o: ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amf.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_amf.h
amf.o: ../include/ipc_gen.h ../include/ais_amf.h ../include/list.h
amf.o: ../include/queue.h ../lcr/lcr_comp.h totempg.h aispoll.h totemsrp.h
amf.o: totem.h totemip.h mempool.h util.h amfconfig.h main.h
amf.o: ../include/saClm.h mainconfig.h objdb.h service.h print.h
ckpt.o: ../include/saAis.h ../include/saCkpt.h ../include/ipc_ckpt.h
ckpt.o: ../include/saAis.h ../include/saCkpt.h ../include/ipc_gen.h
ckpt.o: ../include/list.h ../include/queue.h ../include/hdb.h
ckpt.o: ../lcr/lcr_comp.h aispoll.h service.h totem.h totemip.h objdb.h
ckpt.o: mempool.h util.h main.h ../include/saClm.h ../include/ipc_gen.h
ckpt.o: ../exec/totemip.h mainconfig.h totemsrp.h totempg.h print.h
evt.o: ../include/hdb.h ../include/ipc_evt.h ../include/saAis.h
evt.o: ../include/saEvt.h ../include/saClm.h ../include/ipc_gen.h
evt.o: ../include/list.h ../include/queue.h ../lcr/lcr_comp.h util.h
evt.o: ../include/saAis.h service.h totem.h totemip.h objdb.h aispoll.h
evt.o: mempool.h main.h ../include/saClm.h ../include/ipc_gen.h
evt.o: ../exec/totemip.h mainconfig.h totemsrp.h totempg.h swab.h print.h
lck.o: service.h totem.h totemip.h objdb.h ../include/saAis.h
lck.o: ../include/saLck.h ../include/ipc_lck.h ../include/saAis.h
lck.o: ../include/saLck.h ../include/ipc_gen.h ../include/list.h
lck.o: ../include/queue.h ../lcr/lcr_comp.h aispoll.h mempool.h util.h main.h
lck.o: ../include/saClm.h ../include/ipc_gen.h ../exec/totemip.h mainconfig.h
lck.o: totemsrp.h totempg.h print.h
msg.o: ../include/saAis.h ../include/saMsg.h ../include/ipc_msg.h
msg.o: ../include/saAis.h ../include/saMsg.h ../include/ipc_gen.h
msg.o: ../include/list.h ../include/queue.h ../lcr/lcr_comp.h service.h
msg.o: totem.h totemip.h objdb.h aispoll.h mempool.h util.h main.h
msg.o: ../include/saClm.h ../include/ipc_gen.h ../exec/totemip.h mainconfig.h
msg.o: totemsrp.h totempg.h print.h
cfg.o: ../include/saAis.h ../include/openaisCfg.h ../include/saAis.h
cfg.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_cfg.h
cfg.o: ../include/ipc_gen.h ../include/openaisCfg.h ../include/list.h
cfg.o: ../include/queue.h ../lcr/lcr_comp.h service.h totem.h totemip.h
cfg.o: objdb.h totempg.h aispoll.h totemsrp.h mempool.h util.h print.h
cfg.o: mainconfig.h
cpg.o: ../include/saAis.h ../include/saClm.h ../include/saAis.h
cpg.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_cpg.h
cpg.o: ../include/ipc_gen.h ../include/list.h ../include/queue.h
cpg.o: ../lcr/lcr_comp.h aispoll.h totempg.h totemsrp.h totem.h totemip.h
cpg.o: main.h mainconfig.h objdb.h mempool.h service.h jhash.h swab.h print.h
amfconfig.o: ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amfconfig.o: ../include/ipc_amf.h ../include/ipc_gen.h ../include/ais_amf.h
amfconfig.o: ../include/list.h util.h amfconfig.h aispoll.h mempool.h totem.h
amfconfig.o: totemip.h
aisparser.o: ../lcr/lcr_comp.h objdb.h config.h mempool.h ../include/list.h
vsf_ykd.o: main.h ../include/saAis.h ../include/saClm.h ../include/saAis.h
vsf_ykd.o: ../include/queue.h ../include/ipc_gen.h ../exec/totemip.h
vsf_ykd.o: mainconfig.h ../include/list.h aispoll.h totemsrp.h totem.h
vsf_ykd.o: totemip.h totempg.h objdb.h print.h swab.h vsf.h ../lcr/lcr_comp.h
