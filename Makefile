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

include Makefile.inc

SBINDIR=$(PREFIX)/sbin
INCLUDEDIR=$(PREFIX)/include/openais
INCLUDEDIR_TOTEM=$(PREFIX)/include/openais/totem
INCLUDEDIR_LCR=$(PREFIX)/include/openais/lcr
INCLUDEDIR_SERVICE=$(PREFIX)/include/openais/service
MANDIR=$(PREFIX)/share/man
ETCDIR=/etc
LCRSODIR=$(PREFIX)/libexec/lcrso
ARCH=$(shell uname -p)

ifeq (,$(findstring 64,$(ARCH)))
LIBDIR=$(PREFIX)/lib/openais
else
LIBDIR=$(PREFIX)/lib64/openais
endif
ifeq (s390,$(ARCH))
LIBDIR=$(PREFIX)/lib/openais
endif
ifeq (s390x,$(ARCH))
LIBDIR=$(PREFIX)/lib64/openais
endif
ifeq (ia64,$(ARCH))
LIBDIR=$(PREFIX)/lib/openais
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

AIS_LIBS	= ais SaAmf SaClm SaCkpt SaEvt SaLck SaMsg evs cpg \
		  cfg aisutil

AIS_HEADERS	= saAis.h saAmf.h saClm.h saCkpt.h saEvt.h saEvt.h saLck.h \
		  saMsg.h cpg.h cfg.h evs.h ipc_gen.h mar_gen.h swab.h 	   \
		  ais_util.h

install: all
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_TOTEM)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_LCR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_SERVICE)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(LCRSODIR)
	mkdir -p $(DESTDIR)$(ETCDIR)/ais
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	mkdir -p $(DESTDIR)$(ETCDIR)/ld.so.conf.d


	ln -sf libtotem_pg.so.2.0.0 exec/libtotem_pg.so
	ln -sf libtotem_pg.so.2.0.0 exec/libtotem_pg.so.2
	$(CP) -a exec/libtotem_pg.so $(DESTDIR)$(LIBDIR)
	$(CP) -a exec/libtotem_pg.so.2 $(DESTDIR)$(LIBDIR)
	install -m 755 exec/libtotem_pg.so.2.* $(DESTDIR)$(LIBDIR)

	for aLib in $(AIS_LIBS); do					\
	    ln -sf lib$$aLib.so.2.0.0 lib/lib$$aLib.so;			\
	    ln -sf lib$$aLib.so.2.0.0 lib/lib$$aLib.so.2;		\
	    $(CP) -a lib/lib$$aLib.so $(DESTDIR)$(LIBDIR);		\
	    $(CP) -a lib/lib$$aLib.so.2 $(DESTDIR)$(LIBDIR);		\
	    install -m 755 lib/lib$$aLib.so.2.* $(DESTDIR)$(LIBDIR);	\
	    if [ "xNO" = "x$(STATICLIBS)" ]; then			\
	        install -m 755 lib/lib$$aLib.a $(DESTDIR)$(LIBDIR);	\
		if [ ${OPENAIS_COMPAT} = "DARWIN" ]; then		\
		    ranlib $(DESTDIR)$(LIBDIR)/lib$$aLib.a;		\
	        fi							\
	    fi								\
	done

	echo $(LIBDIR) > $(DESTDIR)$(ETCDIR)/ld.so.conf.d/openais-$(ARCH).conf

	install -m 755 exec/*lcrso $(DESTDIR)$(LCRSODIR)
	install -m 755 exec/aisexec $(DESTDIR)$(SBINDIR)
	install -m 700 exec/keygen $(DESTDIR)$(SBINDIR)/ais-keygen

	if [ ! -f $(DESTDIR)$(ETCDIR)/ais/openais.conf ] ; then 	   \
		install -m 644 conf/openais.conf $(DESTDIR)$(ETCDIR)/ais ; \
	fi
	if [ ! -f $(DESTDIR)$(ETCDIR)/ais/amf.conf ] ; then 		\
		install -m 644 conf/amf.conf $(DESTDIR)$(ETCDIR)/ais ;	\
	fi

	for aHeader in $(AIS_HEADERS); do				\
	    install -m 644 include/$$aHeader $(DESTDIR)$(INCLUDEDIR);	\
	done

	install -m 644 exec/aispoll.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 exec/totempg.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 exec/totem.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 exec/totemip.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 lcr/lcr_ckpt.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 lcr/lcr_comp.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 lcr/lcr_ifact.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 exec/service.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/timer.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/objdb.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/print.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 exec/config.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 include/swab.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 man/*.3 $(DESTDIR)$(MANDIR)/man3
	install -m 644 man/*.5 $(DESTDIR)$(MANDIR)/man5
	install -m 644 man/*.8 $(DESTDIR)$(MANDIR)/man8

doxygen:
	mkdir -p doc/api && doxygen
