# Copyright (c) 2002-2004 MontaVista Software, Inc.
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
LDFLAGS = 

# Debug mode flags
#CFLAGS = -g -Wall
#-DDEBUG
#LDFLAGS = -g

# Profile mode flags
#CFLAGS = -O3 -pg
#LDFLAGS = -pg

EXEC_SRC = main.c clm.c amf.c ckpt.c evt.c parse.c print.c mempool.c
GMI_OBJS = aispoll.c gmi.c tlist.c hdb.c

EXEC_OBJS = main.o clm.o amf.o ckpt.o evt.o parse.o print.o mempool.o libgmi.a

GMI_OBJS = aispoll.o gmi.o tlist.o hdb.o crypto.o

all:libgmi.a libgmi.so.1.0 aisexec keygen

aisexec: $(EXEC_OBJS)
	$(CC) $(LDFLAGS) $(EXEC_OBJS) $(LIBS) -o aisexec

libgmi.a: $(GMI_OBJS)
	$(AR) -rc libgmi.a $(GMI_OBJS)

libgmi.so.1.0: $(GMI_OBJS)
	$(CC) -shared -Wl,-soname,libgmi.so.1 $(GMI_OBJS) -o $@
	rm -f libgmi.so.1 libgmi.so
	ln -s libgmi.so.1.0 libgmi.so.1
	ln -s libgmi.so.1.0 libgmi.so

keygen: keygen.o
	$(CC) $(LDFLAGS) keygen.o -o keygen

clean:
	rm -f *.o aisexec libgmi.so.1.0 libgmi.so.1 libgmi.so libgmi.a gmon.out keygen

depend:
	makedepend -Y -- $(CFLAGS) $(EXEC_SRC) $(GMI_SRC) > /dev/null 2>&1

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<
# DO NOT DELETE

main.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
main.o: ../include/list.h ../include/queue.h gmi.h aispoll.h mempool.h
main.o: parse.h main.h clm.h amf.h handlers.h ckpt.h evt.h hdb.h print.h
clm.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
clm.o: ../include/list.h ../include/queue.h aispoll.h gmi.h parse.h main.h
clm.o: clm.h amf.h handlers.h ckpt.h evt.h hdb.h print.h mempool.h
amf.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
amf.o: ../include/list.h ../include/queue.h gmi.h aispoll.h mempool.h parse.h
amf.o: main.h clm.h amf.h handlers.h ckpt.h evt.h hdb.h print.h
ckpt.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
ckpt.o: ../include/list.h ../include/queue.h aispoll.h mempool.h parse.h
ckpt.o: main.h clm.h amf.h handlers.h ckpt.h evt.h hdb.h print.h gmi.h
evt.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
evt.o: ../include/list.h ../include/queue.h aispoll.h mempool.h parse.h
evt.o: main.h clm.h amf.h handlers.h ckpt.h evt.h hdb.h print.h gmi.h
parse.o: ../include/ais_types.h ../include/list.h parse.h aispoll.h mempool.h
print.o: print.h ../include/ais_types.h parse.h ../include/list.h aispoll.h
mempool.o: ../include/list.h mempool.h
crypto.o: crypto.h
