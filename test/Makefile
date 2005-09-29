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

LIBRARIES= ../lib/libSaClm.a ../lib/libSaAmf.a ../lib/libSaCkpt.a ../lib/libSaEvt.a ../lib/libSaLck.a ../lib/libevs.a
LIBS = $(LIBRARIES) -lpthread

# Production mode flags
#CFLAGS = -c -O3 -Wall -I../include
#CFLAGS = -c -O3 -Wall -I../include
#LDFLAGS = -L../lib
#LIBRARIES= ../lib/libais.a ../lib/libevs.a
#LIBS = $(LIBRARIES) -lpthread

# Debug mode flags
CFLAGS = -c -g -Wall -DDEBUG -I../include
CPPFLAGS = -c -g -Wall -DDEBUG -I../include
LDFLAGS = -g -L../lib

# Profile mode flags
#CFLAGS = -c -O3 -pg -DDEBUG -I../include
#LDFLAGS = -pg -L../lib

EXTRA_CFLAGS = -I../include
TEST_SRC =  testclm.c testamf.c testamf1.c testamf2.c testamf3.c \
		testamf4.c testamf5.c testamf6.c testamfth.c  \
		testckpt.c ckptstress.c ckptbench.c  \
		ckptbenchth.c testevt.c testevs.c evsbench.c \
		subscription.c publish.c evtbench.c \
		sa_error.c unlink.c testclm2.c testlck.c

all: testclm testamf testamf1 testamf2 testamf3 testamf4 testamf5 \
	testamf6 testamfth testckpt ckptstress ckptbench \
	ckptbenchth ckpt-rd ckpt-wr testevt testevs \
	evsbench subscription publish evtbench unlink testclm2 testlck

testtimer: testtimer.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testtimer testtimer.o ../exec/timer.o

testamf: testamf.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf testamf.o $(LIBS)

testamf1: testamf1.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf1 testamf1.o $(LIBS)

testamf2: testamf2.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf2 testamf2.o $(LIBS)

testamf3: testamf3.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf3 testamf3.o $(LIBS)

testamf4: testamf4.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf4 testamf4.o $(LIBS)

testamf5: testamf5.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf5 testamf5.o $(LIBS)

testamf6: testamf6.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamf6 testamf6.o $(LIBS)

testamfth: testamfth.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testamfth testamfth.o $(LIBS)

testevt: testevt.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testevt testevt.o sa_error.o $(LIBS)

testevs: testevs.o $(LIBS)
	$(CC) $(LDFLAGS) -o testevs testevs.o $(LIBS)

testevsth: testevsth.o $(LIBS)
	$(CC) $(LDFLAGS) -o testevsth testevsth.o $(LIBS)

evsbench: evsbench.o $(LIBS)
	$(CC) $(LDFLAGS) -o evsbench evsbench.o $(LIBS)

testclm: testclm.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testclm testclm.o $(LIBS)

testckpt: testckpt.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testckpt testckpt.o sa_error.o $(LIBS)

ckptbench: ckptbench.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o ckptbench ckptbench.o $(LIBS)

ckptbenchth: ckptbenchth.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o ckptbenchth ckptbenchth.o $(LIBS)

ckptstress: ckptstress.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o ckptstress ckptstress.o $(LIBS)

subscription: subscription.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o subscription subscription.o sa_error.o $(LIBS)

publish: publish.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o publish publish.o sa_error.o $(LIBS)

unlink: unlink.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o unlink unlink.o sa_error.o $(LIBS)

evtbench: evtbench.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o evtbench evtbench.o sa_error.o $(LIBS)

test: test.o $(LIBRARIES)
	g++ $(LDFLAGS) -o test test.o $(LIBS)

ckpt-rd: ckpt-rd.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o ckpt-rd ckpt-rd.o sa_error.o $(LIBS)

ckpt-wr: ckpt-wr.o sa_error.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o ckpt-wr ckpt-wr.o sa_error.o $(LIBS)

testclm2: testclm2.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testclm2 testclm2.o $(LIBS)

testlck: testlck.o $(LIBRARIES)
	$(CC) $(LDFLAGS) -o testlck testlck.o $(LIBS)

clean:
	rm -f *.o testclm testamf testamf1 testamf2 testamf3 testamf4 \
	testamf5 testamf6 testamfth testckpt ckptstress testtimer \
	ckptbench ckptbenchth testevt testevs ckpt-wr ckpt-rd \
	evsbench subscription publish evtbench unlink
%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

depend:
	makedepend -Y -- $(CFLAGS) $(TEST_SRC) > /dev/null 2>&1
# DO NOT DELETE

testclm.o: ../include/saAis.h ../include/saClm.h
testamf.o: ../include/saAis.h ../include/ais_amf.h ../include/saClm.h
testamf1.o: ../include/saAis.h ../include/ais_amf.h
testamf2.o: ../include/saAis.h ../include/ais_amf.h
testamf3.o: ../include/saAis.h ../include/ais_amf.h
testamf4.o: ../include/saAis.h ../include/ais_amf.h
testamf5.o: ../include/saAis.h ../include/ais_amf.h
testamf6.o: ../include/saAis.h ../include/ais_amf.h
testamfth.o: ../include/saAis.h ../include/ais_amf.h ../include/saClm.h
testckpt.o: ../include/saAis.h ../include/saCkpt.h sa_error.h
ckptstress.o: ../include/saAis.h ../include/saCkpt.h
ckptbench.o: ../include/saAis.h ../include/saCkpt.h
ckptbenchth.o: ../include/saAis.h ../include/saCkpt.h
testevt.o: ../include/saAis.h ../include/saEvt.h
testevs.o: ../include/evs.h
evsbench.o: ../include/saAis.h ../include/evs.h
subscription.o: ../include/saAis.h ../include/saEvt.h
publish.o: ../include/saAis.h ../include/saEvt.h
evtbench.o: ../include/saAis.h ../include/saEvt.h
sa_error.o: ../include/saAis.h
unlink.o: ../include/saAis.h ../include/saEvt.h
