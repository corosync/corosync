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
SBINDIR=/usr/sbin
INCLUDEDIR=/usr/include/openais
INCLUDEDIR_TOTEM=/usr/include/openais/totem
INCLUDEDIR_LCR=/usr/include/openais/lcr
INCLUDEDIR_SERVICE=/usr/include/openais/service
MANDIR=/usr/share/man
ETCDIR=/etc/ais
LCRSODIR=/usr/libexec/lcrso
ARCH=$(shell uname -p)

ifeq (,$(findstring 64,$(ARCH)))
LIBDIR=/usr/lib/openais
else
LIBDIR=/usr/lib64/openais
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
	rm -rf doc/api

install:
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_TOTEM)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_LCR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_SERVICE)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(LCRSODIR)
	mkdir -p $(DESTDIR)$(ETCDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	mkdir -p $(DESTDIR)/etc/ld.so.conf.d

	ln -sf libais.so.1.0.0 lib/libais.so
	ln -sf libSaAmf.so.1.0.0 lib/libSaAmf.so
	ln -sf libSaClm.so.1.0.0 lib/libSaClm.so
	ln -sf libSaCkpt.so.1.0.0 lib/libSaCkpt.so
	ln -sf libSaEvt.so.1.0.0 lib/libSaEvt.so
	ln -sf libSaLck.so.1.0.0 lib/libSaLck.so
	ln -sf libSaMsg.so.1.0.0 lib/libSaMsg.so
	ln -sf libevs.so.1.0.0 lib/libevs.so
	ln -sf libcpg.so.1.0.0 lib/libcpg.so
	ln -sf libtotem_pg.so.1.0.0 exec/libtotem_pg.so

	ln -sf libais.so.1.0.0 lib/libais.so.1
	ln -sf libSaAmf.so.1.0.0 lib/libSaAmf.so.1
	ln -sf libSaClm.so.1.0.0 lib/libSaClm.so.1
	ln -sf libSaCkpt.so.1.0.0 lib/libSaCkpt.so.1
	ln -sf libSaEvt.so.1.0.0 lib/libSaEvt.so.1
	ln -sf libSaLck.so.1.0.0 lib/libSaLck.so.1
	ln -sf libSaMsg.so.1.0.0 lib/libSaMsg.so.1
	ln -sf libevs.so.1.0.0 lib/libevs.so.1
	ln -sf libcpg.so.1.0.0 lib/libcpg.so.1
	ln -sf libtotem_pg.so.1.0.0 exec/libtotem_pg.so.1

	cp -a lib/libais.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaAmf.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaCkpt.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaEvt.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaLck.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaMsg.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libevs.so $(DESTDIR)$(LIBDIR)
	cp -a lib/libcpg.so $(DESTDIR)$(LIBDIR)
	cp -a exec/libtotem_pg.so $(DESTDIR)$(LIBDIR)

	cp -a lib/libais.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaAmf.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaCkpt.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaEvt.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaLck.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libSaMsg.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libevs.so.1 $(DESTDIR)$(LIBDIR)
	cp -a lib/libcpg.so.1 $(DESTDIR)$(LIBDIR)
	cp -a exec/libtotem_pg.so.1 $(DESTDIR)$(LIBDIR)

	install -m 755 lib/libais.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaAmf.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaClm.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaCkpt.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaEvt.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaLck.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaMsg.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libevs.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libcpg.so.1.* $(DESTDIR)$(LIBDIR)
	install -m 755 exec/libtotem_pg.so.1.* $(DESTDIR)$(LIBDIR)

ifneq "NO" "$(STATICLIBS)"
	install -m 755 lib/libais.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaAmf.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaClm.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaCkpt.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaEvt.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaLck.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libSaMsg.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libevs.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libcpg.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libevs.a $(DESTDIR)$(LIBDIR)
	install -m 755 lib/libcpg.a $(DESTDIR)$(LIBDIR)
	install -m 755 exec/libtotem_pg.a $(DESTDIR)$(LIBDIR)
endif

	echo $(LIBDIR) > $(DESTDIR)/etc/ld.so.conf.d/openais-$(ARCH).conf

	install -m 755 exec/*lcrso $(DESTDIR)$(LCRSODIR)

	install -m 755 exec/aisexec $(DESTDIR)$(SBINDIR)
	install -m 700 exec/keygen $(DESTDIR)$(SBINDIR)/ais-keygen
	install -m 644 conf/openais.conf $(DESTDIR)$(ETCDIR)
	install -m 644 conf/amf.conf $(DESTDIR)$(ETCDIR)

	install -m 644 include/saAis.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saAmf.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saClm.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saCkpt.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saEvt.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saEvt.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saLck.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/saMsg.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/cpg.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/evs.h $(DESTDIR)$(INCLUDEDIR)
	install -m 644 exec/aispoll.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 exec/totempg.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 exec/totem.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 exec/totemip.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 lcr/lcr_ckpt.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 lcr/lcr_comp.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 lcr/lcr_ifact.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 exec/service.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/objdb.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/print.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/config.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 include/swab.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 man/*.3 $(DESTDIR)$(MANDIR)/man3
	install -m 644 man/*.5 $(DESTDIR)$(MANDIR)/man5
	install -m 644 man/*.8 $(DESTDIR)$(MANDIR)/man8

doxygen:
	doxygen
