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
CFLAGS = -O3 -Wall
LDFLAGS = 

# Debug mode flags
#CFLAGS = -g -DDEBUG -Wall
#LDFLAGS = -g

# Profile mode flags
#CFLAGS = -O3 -pg -DDEBUG
#LDFLAGS = -pg

# Code Coverage with lcov flgs
#CFLAGS = -ftest-coverage -fprofile-arcs
#LDFLAGS = -g


CFLAGS += -fPIC

all:libais.a libais.so.1.0 libevs.a libevs.so.1.0

LIBAIS_SRC = util.c amf.c clm.c ckpt.c evt.c

LIBEVS_SRC = util.c evs.c

libais.a: util.o amf.o clm.o ckpt.o evt.o
	$(AR) -rc libais.a util.o amf.o clm.o ckpt.o evt.o

libais.so.1.0: util.o amf.o clm.o ckpt.o evt.o
	$(CC) -shared -Wl,-soname,libais.so.1 util.o amf.o clm.o ckpt.o \
								evt.o -o $@
	rm -f libais.so.1 libais.so
	ln -s libais.so.1.0 libais.so.1
	ln -s libais.so.1.0 libais.so
	
libevs.a: util.o amf.o evs.o
	$(AR) -rc libevs.a util.o evs.o

libevs.so.1.0: util.o evs.o
	$(CC) -shared -Wl,-soname,libevs.so.1 util.o evs.o -o $@
	rm -f libevs.so.1 libevs.so
	ln -s libevs.so.1.0 libevs.so.1
	ln -s libevs.so.1.0 libevs.so

clean:
	rm -f *.o libais.so* libais.a libevs.so* libevs.a *.da *.bb *.bbg

depend:
	makedepend -Y -- $(CFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
# DO NOT DELETE

util.o: ../include/ais_types.h ../include/ais_msg.h ../include/ais_types.h
util.o: util.h ../include/ais_msg.h ../include/evs.h
amf.o: ../include/ais_types.h ../include/ais_amf.h ../include/ais_types.h
amf.o: ../include/ais_msg.h util.h
clm.o: ../include/ais_types.h ../include/saClm.h ../include/ais_types.h
clm.o: ../include/ais_msg.h util.h
ckpt.o: ../include/list.h ../include/ais_types.h ../include/ais_ckpt.h
ckpt.o: ../include/ais_types.h ../include/ais_msg.h util.h
evt.o: ../include/ais_evt.h ../include/ais_types.h ../include/ais_msg.h
evt.o: util.h
