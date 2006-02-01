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

# BUILD_DYNAMIC can be defined to 1 to build for dynamic loading of service
#       handler modules.  If the developer intends to debug, building without
#       dynamic modules should provide an easier route.
BUILD_DYNAMIC=0

# Production mode flags
CFLAGS = -O3 -Wall
LDFLAGS = -lpthread
DESTDIR=/usr/local/openais

# Debug mode flags
#CFLAGS = -g
#LDFLAGS = -g -lpthread

# Profile mode flags
#CFLAGS = -O3 -pg
#LDFLAGS = -pg

ifeq (${BUILD_DYNAMIC}, 1)
CFLAGS += -DBUILD_DYNAMIC=1 -fPIC
LDFLAGS += -ldl -rdynamic
endif

all:
	(cd lcr; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)");
	(cd lib; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)");
	(cd exec; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" BUILD_DYNAMIC="$(BUILD_DYNAMIC)");
	(cd test; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)");

clean:
	(cd lcr; echo ==== `pwd` ===; $(MAKE) clean);
	(cd lib; echo ==== `pwd` ===; $(MAKE) clean);
	(cd exec; echo ==== `pwd` ===; $(MAKE) clean);
	(cd test; echo ==== `pwd` ===; $(MAKE) clean);

install:
	mkdir -p $(DESTDIR)/sbin
	mkdir -p $(DESTDIR)/usr/include
	mkdir -p $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/etc/ais

	cp -a lib/libais.a $(DESTDIR)/usr/lib
	cp -a lib/libais.so* $(DESTDIR)/usr/lib
	cp -a lib/libSa*.a $(DESTDIR)/usr/lib
	cp -a lib/libSa*.so* $(DESTDIR)/usr/lib
	cp -a lib/libevs.a $(DESTDIR)/usr/lib
	cp -a lib/libevs.so* $(DESTDIR)/usr/lib
	cp -a exec/libtotem_pg* $(DESTDIR)/usr/lib

	install -m 755 exec/aisexec $(DESTDIR)/sbin
	install -m 755 exec/keygen $(DESTDIR)/sbin/ais-keygen
	install -m 755 conf/openais.conf $(DESTDIR)/etc
	install -m 755 conf/groups.conf $(DESTDIR)/etc

	cp -a include/saAis.h $(DESTDIR)/usr/include
	cp -a include/ais_amf.h $(DESTDIR)/usr/include
	cp -a include/saClm.h $(DESTDIR)/usr/include
	cp -a include/saCkpt.h $(DESTDIR)/usr/include
	cp -a include/saEvt.h $(DESTDIR)/usr/include
	cp -a include/evs.h $(DESTDIR)/usr/include
	cp -a exec/totem.h $(DESTDIR)/usr/include
