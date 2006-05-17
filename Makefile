# Copyright (c) 2002-2006 MontaVista Software, Inc.
# Copyright (c) 2006 Red Hat, Inc.
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

DESTDIR=/usr/local
ifeq "$(DESTDIR)" "/usr/local"
SBINDIR=${DESTDIR}/usr/sbin
INCLUDEDIR=${DESTDIR}/usr/include/openais
INCLUDEDIR_TOTEM=${DESTDIR}/usr/include/openais/totem
INCLUDEDIR_LCR=${DESTDIR}/usr/include/openais/lcr
MANDIR=/usr/share/man
else
SBINDIR=${DESTDIR}/sbin
INCLUDEDIR=${DESTDIR}/include/openais
INCLUDEDIR_TOTEM=${DESTDIR}/include/openais/totem
INCLUDEDIR_LCR=${DESTDIR}/include/openais/lcr
MANDIR=$(DESTDIR)/man
endif
ETCDIR=/etc

ifeq "$(DESTDIR)" "/"
ifeq "" "$(findstring 64,$(ARCH))"
LIBDIR=${DESTDIR}/usr/lib64/openais
LCRSODIR=$(DESTDIR)/usr/lib64/openais/lcrso
else
LIBDIR=${DESTDIR}/usr/lib/openais
LCRSODIR=$(DESTDIR)/usr/lib/openais/lcrso
endif
else
ifeq "" "$(findstring 64,$(ARCH))"
LIBDIR=${DESTDIR}/lib64/openais
LCRSODIR=$(DESTDIR)/lib64/openais/lcrso
else
LIBDIR=${DESTDIR}/lib/openais
LCRSODIR=$(DESTDIR)/lib/openais/lcrso
endif
endif


all:
	(cd lcr; echo ==== `pwd` ===; $(MAKE) all);
	(cd lib; echo ==== `pwd` ===; $(MAKE) all);
	(cd exec; echo ==== `pwd` ===; $(MAKE) all);
	(cd test; echo ==== `pwd` ===; $(MAKE) all);

clean:
	(cd lcr; echo ==== `pwd` ===; $(MAKE) clean);
	(cd lib; echo ==== `pwd` ===; $(MAKE) clean);
	(cd exec; echo ==== `pwd` ===; $(MAKE) clean);
	(cd test; echo ==== `pwd` ===; $(MAKE) clean);

install:
	mkdir -p $(SBINDIR)
	mkdir -p $(INCLUDEDIR)
	mkdir -p $(INCLUDEDIR_TOTEM)
	mkdir -p $(INCLUDEDIR_LCR)
	mkdir -p $(LIBDIR)
	mkdir -p $(LCRSODIR)
	mkdir -p $(ETCDIR)
	mkdir -p $(MANDIR)/man3
	mkdir -p $(MANDIR)/man8
	mkdir -p /etc/ld.so.conf.d

	install -m 755 lib/libais.a $(LIBDIR)
	install -m 755 lib/libais.so* $(LIBDIR)
	install -m 755 lib/libSa*.a $(LIBDIR)
	install -m 755 lib/libSa*.so* $(LIBDIR)
	install -m 755 lib/libevs.a $(LIBDIR)
	install -m 755 lib/libevs.so* $(LIBDIR)
	install -m 755 lib/libcpg.a $(LIBDIR)
	install -m 755 lib/libcpg.so* $(LIBDIR)
	echo $(LIBDIR) > /etc/ld.so.conf.d/"openais-`uname -p`.conf"
	echo $(LCRSODIR) >> /etc/ld.so.conf.d/"openais-`uname -p`.conf"

	cp exec/libtotem_pg* $(LIBDIR)
	cp exec/*lcrso $(LCRSODIR)

	install -m 755 exec/aisexec $(SBINDIR)
	install -m 755 exec/keygen $(SBINDIR)/ais-keygen
	install -m 755 conf/openais.conf $(ETCDIR)
	install -m 755 conf/amf.conf $(ETCDIR)

	install -m 644 include/saAis.h $(INCLUDEDIR)
	install -m 644 include/saAmf.h $(INCLUDEDIR)
	install -m 644 include/saClm.h $(INCLUDEDIR)
	install -m 644 include/saCkpt.h $(INCLUDEDIR)
	install -m 644 include/saEvt.h $(INCLUDEDIR)
	install -m 644 include/saEvt.h $(INCLUDEDIR)
	install -m 644 include/saLck.h $(INCLUDEDIR)
	install -m 644 include/saMsg.h $(INCLUDEDIR)
	install -m 644 include/cpg.h $(INCLUDEDIR)
	install -m 644 include/evs.h $(INCLUDEDIR)
	install -m 644 exec/aispoll.h $(INCLUDEDIR_TOTEM)
	install -m 644 exec/totem.h $(INCLUDEDIR_TOTEM)
	install -m 644 exec/totemip.h $(INCLUDEDIR_TOTEM)
	install -m 644 lcr/lcr_ckpt.h $(INCLUDEDIR_LCR)
	install -m 644 lcr/lcr_comp.h $(INCLUDEDIR_LCR)
	install -m 644 lcr/lcr_ifact.h $(INCLUDEDIR_LCR)
	install -m 644 man/*.3 $(MANDIR)/man3
	install -m 644 man/*.8 $(MANDIR)/man8
	/sbin/ldconfig
