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
#CFLAGS = -O3 -Wall
#LDFLAGS = 

# Debug mode flags
CFLAGS = -g -DDEBUG -Wall
LDFLAGS = -g

# Profile mode flags
#CFLAGS = -O3 -pg -DDEBUG
#LDFLAGS = -pg

# Code Coverage with lcov flgs
#CFLAGS = -ftest-coverage -fprofile-arcs
#LDFLAGS = -g


CFLAGS += -fPIC

all:libSaClm.a libSaClm.so.1.0 libSaAmf.a libSaAmf.so.1.0 libSaCkpt.a \
	libSaCkpt.so.1.0 libSaEvt.a libSaEvt.so.1.0 libais.a libais.so.1.0 libevs.a \
	libevs.so.1.0

LIBAIS_SRC = util.c amf.c clm.c ckpt.c evt.c

LIBEVS_SRC = util.c evs.c

libSaClm.a: util.o clm.o
	$(AR) -rc libSaClm.a util.o clm.o

libSaClm.so.1.0: util.o clm.o
	$(CC) -shared -Wl,-soname,libSaClm.so.1 util.o clm.o -o $@
	rm -f libSaClm.so.1 libSaClm.so
	ln -s libSaClm.so.1.0 libSaClm.so.1
	ln -s libSaClm.so.1.0 libSaClm.so

libSaAmf.a: util.o amf.o
	$(AR) -rc libSaAmf.a util.o amf.o

libSaAmf.so.1.0: util.o amf.o
	$(CC) -shared -Wl,-soname,libSaAmf.so.1 util.o amf.o -o $@
	rm -f libSaAmf.so.1 libSaAmf.so
	ln -s libSaAmf.so.1.0 libSaAmf.so.1
	ln -s libSaAmf.so.1.0 libSaAmf.so

libSaCkpt.a: util.o ckpt.o
	$(AR) -rc libSaCkpt.a util.o ckpt.o

libSaCkpt.so.1.0: util.o ckpt.o
	$(CC) -shared -Wl,-soname,libSaCkpt.so.1 util.o ckpt.o -o $@
	rm -f libSaCkpt.so.1 libSaCkpt.so
	ln -s libSaCkpt.so.1.0 libSaCkpt.so.1
	ln -s libSaCkpt.so.1.0 libSaCkpt.so

libSaEvt.a: util.o evt.o
	$(AR) -rc libSaEvt.a util.o evt.o

libSaEvt.so.1.0: util.o ckpt.o
	$(CC) -shared -Wl,-soname,libSaEvt.so.1 util.o evt.o -o $@
	rm -f libSaEvt.so.1 libSaEvt.so
	ln -s libSaEvt.so.1.0 libSaEvt.so.1
	ln -s libSaEvt.so.1.0 libSaEvt.so

libais.a: util.o amf.o clm.o ckpt.o evt.o
	$(AR) -rc libais.a util.o amf.o clm.o ckpt.o evt.o

libais.so.1.0: util.o amf.o clm.o ckpt.o evt.o
	$(CC) -shared -Wl,-soname,libais.so.1 util.o amf.o clm.o ckpt.o \
								evt.o -o $@
	rm -f libais.so.1 libais.so
	ln -s libais.so.1.0 libais.so.1
	ln -s libais.so.1.0 libais.so
	
libevs.a: util.o evs.o
	$(AR) -rc libevs.a util.o evs.o

libevs.so.1.0: util.o evs.o
	$(CC) -shared -Wl,-soname,libevs.so.1 util.o evs.o -o $@
	rm -f libevs.so.1 libevs.so
	ln -s libevs.so.1.0 libevs.so.1
	ln -s libevs.so.1.0 libevs.so

clean:
	rm -f *.o libais.so* libais.a libSaClm.so* libSaClm.a* libSaAmf.so* libSaAmf.a \
		libSaCkpt.so* libSaCkpt.a* libSaEvt.so* libSaEvt.a libevs.so* libevs.a \
		*.da *.bb *.bbg

depend:
	makedepend -Y -- $(CFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
# DO NOT DELETE

util.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
util.o: ../include/evs.h ../include/saClm.h ../include/ipc_gen.h util.h
amf.o: ../include/ais_types.h ../include/ais_amf.h ../include/ais_types.h
amf.o: ../include/ais_msg.h ../include/evs.h ../include/saClm.h
amf.o: ../include/ipc_gen.h util.h
clm.o: ../include/ais_types.h ../include/saClm.h ../include/ais_types.h
clm.o: ../include/ais_msg.h ../include/evs.h ../include/saClm.h
clm.o: ../include/ipc_gen.h ../include/ipc_clm.h util.h
ckpt.o: ../include/list.h ../include/ais_types.h ../include/saCkpt.h
ckpt.o: ../include/ais_types.h ../include/ais_msg.h ../include/evs.h
ckpt.o: ../include/saClm.h ../include/ipc_gen.h util.h
evt.o: ../include/ipc_evt.h ../include/ais_types.h ../include/saEvt.h
evt.o: ../include/saClm.h ../include/ipc_gen.h util.h ../include/ais_msg.h
evt.o: ../include/evs.h ../exec/totempg.h ../exec/aispoll.h
evt.o: ../exec/totemsrp.h
