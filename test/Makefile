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
#CFLAGS = -c -O3 -Wall -I../include
#LDFLAGS = -L../lib
LIBS = ../lib/libais.a -lpthread

# Debug mode flags
CFLAGS = -c -g -Wall -DDEBUG -I../include
LDFLAGS = -g -L../lib
LIBS = ../lib/libais.a -lpthread

# Profile mode flags
#CFLAGS = -c -O3 -pg -DDEBUG -I../include
#LDFLAGS = -pg -L../lib
#LIBS = ../lib/libais.a

EXTRA_CFLAGS = -I../include
all: testclm testamf testamf1 testamf2 testamf3 testamf4 testamf5 testamf6 testamfth testckpt ckptstress testparse

testparse: testparse.o
	$(CC) $(LDFLAGS) -o testparse testparse.o ../exec/parse.o ../exec/print.o ../exec/mempool.o

testtimer: testtimer.o
	$(CC) $(LDFLAGS) -o testtimer testtimer.o ../exec/timer.o

testamf: testamf.o
	$(CC) $(LDFLAGS) -o testamf testamf.o $(LIBS)

testamf1: testamf1.o
	$(CC) $(LDFLAGS) -o testamf1 testamf1.o $(LIBS)

testamf2: testamf2.o
	$(CC) $(LDFLAGS) -o testamf2 testamf2.o $(LIBS)

testamf3: testamf3.o
	$(CC) $(LDFLAGS) -o testamf3 testamf3.o $(LIBS)

testamf4: testamf4.o
	$(CC) $(LDFLAGS) -o testamf4 testamf4.o $(LIBS)

testamf5: testamf5.o
	$(CC) $(LDFLAGS) -o testamf5 testamf5.o $(LIBS)

testamf6: testamf6.o
	$(CC) $(LDFLAGS) -o testamf6 testamf6.o $(LIBS)

testamfth: testamfth.o
	$(CC) $(LDFLAGS) -o testamfth testamfth.o $(LIBS)

testclm: testclm.o
	$(CC) $(LDFLAGS) -o testclm testclm.o $(LIBS)

testckpt: testckpt.o
	$(CC) $(LDFLAGS) -o testckpt testckpt.o $(LIBS)

ckptstress: ckptstress.o
	$(CC) $(LDFLAGS) -o ckptstress ckptstress.o $(LIBS)

clean:
	rm -f *.o testclm testamf testamf1 testamf2 testamf3 testamf4 testamf5 testamf6 testamfth testckpt ckptstress testparse testtimer

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<
