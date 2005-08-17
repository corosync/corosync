# Copyright (c) 2002-2005 MontaVista Software, Inc.
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

# Production mode flags
CFLAGS = -O3 -Wall -fomit-frame-pointer
LDFLAGS = -lpthread

# Debug mode flags
#CFLAGS = -g -Wall
##-DDEBUG
#LDFLAGS = -g -lpthread

# Profile mode flags
#CFLAGS = -O3 -pg
#LDFLAGS = -pg -lpthread

# Code Coverage with lcov flgs
#CFLAGS = -ftest-coverage -fprofile-arcs
#LDFLAGS = -g

TOTEM_SRC = aispoll.c totemnet.c totemrrp.c totemsrp.c totemmrp.c totempg.c totemconfig.c tlist.c hdb.c crypto.c wthread.c
TOTEM_OBJS = aispoll.o totemnet.o totemrrp.o totemsrp.o totemmrp.o totempg.o totemconfig.o tlist.o hdb.o crypto.o wthread.o

EXEC_SRC = main.c clm.c amf.c ckpt.c evt.c evs.c mainparse.c print.c mempool.c \
		util.c sync.c
EXEC_OBJS = main.o clm.o amf.o ckpt.o evt.o evs.o mainparse.o print.o mempool.o \
		util.o sync.o
EXEC_LIBS = libtotem_pg.a

OBJS = $(TOTEM_OBJS) $(EXEC_OBJS)

all:libtotem_pg.a libtotem_pg.so.1.0 aisexec keygen

aisexec: $(EXEC_OBJS) libtotem_pg.a
	$(CC) $(LDFLAGS) $(EXEC_OBJS) $(EXEC_LIBS) -o aisexec

libtotem_pg.a: $(TOTEM_OBJS)
	$(AR) -rc libtotem_pg.a $(TOTEM_OBJS)

libtotem_pg.so.1.0: $(TOTEM_OBJS)
	$(CC) -shared -Wl,-soname,libtotem_pg.so.1 $(TOTEM_OBJS) -o $@
	rm -f libtotem_pg.so.1 libtotem_pg.so
	ln -s libtotem_pg.so.1.0 libtotem_pg.so.1
	ln -s libtotem_pg.so.1.0 libtotem_pg.so

keygen: keygen.o
	$(CC) $(LDFLAGS) keygen.o -o keygen

clean:
	rm -f aisexec $(OBJS) libtotem_pg.so.1.0 libtotem_pg.so.1 libtotem_pg.so libtotem_pg.a gmon.out keygen keygen.o *.da *.bb *.bbg

depend:
	makedepend -Y -- $(CFLAGS) $(EXEC_SRC) $(TOTEM_SRC) > /dev/null 2>&1

# -fPIC rules required for lib totem
aispoll.o: aispoll.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

totempg.o: totempg.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

totemsrp.o: totemsrp.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

totemrrp.o: totemrrp.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

totemnet.o: totemnet.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

wthread.o: wthread.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

tlist.o: tlist.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

hdb.o: hdb.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

crypto.o: crypto.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

totemmrp.o: totemmrp.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

totemconfig.o: totemconfig.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $(*F).c

# DO NOT DELETE

main.o: ../include/ais_types.h ../include/ipc_evs.h ../include/ais_types.h
main.o: evs.h ../include/saClm.h ../include/ipc_gen.h ../include/list.h
main.o: ../include/queue.h totempg.h aispoll.h totemsrp.h mempool.h mainparse.h
main.o: main.h clm.h ../include/saClm.h amf.h handlers.h ckpt.h evt.h hdb.h
main.o: ../include/saEvt.h swab.h print.h
clm.o: ../include/ais_types.h ../include/saClm.h ../include/ais_types.h
clm.o: ../include/ipc_evs.h evs.h ../include/saClm.h ../include/ipc_gen.h
clm.o: ../include/ipc_clm.h ../include/list.h ../include/queue.h aispoll.h
clm.o: totempg.h totemsrp.h mainparse.h main.h clm.h amf.h handlers.h ckpt.h
clm.o: evt.h hdb.h ../include/saEvt.h mempool.h print.h
amf.o: ../include/ais_types.h ../include/ipc_evs.h ../include/ais_types.h
amf.o: evs.h ../include/saClm.h ../include/ipc_gen.h ../include/list.h
amf.o: ../include/queue.h totempg.h aispoll.h totemsrp.h mempool.h util.h
amf.o: mainparse.h main.h clm.h ../include/saClm.h amf.h handlers.h ckpt.h evt.h
amf.o: hdb.h ../include/saEvt.h print.h
ckpt.o: ../include/ais_types.h ../include/ipc_evs.h ../include/ais_types.h
ckpt.o: evs.h ../include/saClm.h ../include/ipc_gen.h ../include/list.h
ckpt.o: ../include/queue.h aispoll.h mempool.h util.h mainparse.h totempg.h
ckpt.o: totemsrp.h main.h clm.h ../include/saClm.h amf.h handlers.h ckpt.h
ckpt.o: evt.h hdb.h ../include/saEvt.h print.h
evt.o: ../include/ipc_evt.h ../include/ais_types.h ../include/saEvt.h
evt.o: ../include/saClm.h ../include/ipc_gen.h ../include/list.h
evt.o: ../include/queue.h util.h ../include/ais_types.h aispoll.h mempool.h
evt.o: mainparse.h totempg.h totemsrp.h main.h evs.h clm.h ../include/saClm.h
evt.o: amf.h ../include/ipc_evs.h handlers.h ckpt.h evt.h hdb.h
evt.o: ../include/saEvt.h swab.h print.h
evs.o: ../include/ais_types.h ../include/ipc_evs.h ../include/ais_types.h
evs.o: evs.h ../include/saClm.h ../include/ipc_gen.h ../include/list.h
evs.o: ../include/queue.h aispoll.h totempg.h totemsrp.h mainparse.h main.h clm.h
evs.o: ../include/saClm.h amf.h handlers.h ckpt.h evt.h hdb.h
evs.o: ../include/saEvt.h mempool.h print.h
mainparse.o: ../include/ais_types.h ../include/list.h util.h mainparse.h aispoll.h
mainparse.o: totempg.h totemsrp.h mempool.h print.h ../include/saClm.h
mainparse.o: ../include/ais_types.h
print.o: print.h ../include/ais_types.h ../include/saClm.h
print.o: ../include/ais_types.h mainparse.h ../include/list.h aispoll.h totempg.h
print.o: totemsrp.h
mempool.o: ../include/list.h mempool.h
util.o: ../include/ais_types.h ../include/list.h util.h
aispoll.o: aispoll.h ../include/list.h hdb.h ../include/ais_types.h tlist.h
totemsrp.o: aispoll.h totemsrp.h ../include/queue.h ../include/sq.h
totemsrp.o: ../include/list.h hdb.h ../include/ais_types.h swab.h crypto.h
totempg.o: totempg.h aispoll.h totemsrp.h swab.h
tlist.o: ../include/list.h tlist.h
hdb.o: hdb.h ../include/ais_types.h
crypto.o: crypto.h
