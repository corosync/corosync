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

builddir:=$(CURDIR)/
ifneq ($(O),)
# cleanup the path (make it absolute)
builddir:=$(abspath $(O))/
ifeq ($(builddir),)
builddir:=$(O)
$(warning your abspath function is not working)
$(warning > setting builddir to $(builddir))
endif
endif

THIS_MAKEFILE:=$(realpath $(lastword $(MAKEFILE_LIST)))

ifeq ($(THIS_MAKEFILE),)
srcdir:=$(CURDIR)/
$(warning your realpath function is not working)
$(warning > setting srcdir to $(srcdir))
else
srcdir:=$(dir $(THIS_MAKEFILE))
endif

include $(srcdir)Makefile.inc

SBINDIR=$(PREFIX)/sbin
INCLUDEDIR=$(PREFIX)/include/openais
INCLUDEDIR_TOTEM=$(PREFIX)/include/openais/totem
INCLUDEDIR_LCR=$(PREFIX)/include/openais/lcr
INCLUDEDIR_SERVICE=$(PREFIX)/include/openais/service
MANDIR=$(PREFIX)/share/man
ETCDIR=/etc
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

SUBDIRS:=$(builddir)lcr $(builddir)lib $(builddir)exec $(builddir)test
sub_make = srcdir=$(srcdir) builddir=$(builddir) subdir=$(1)/ $(MAKE) -I$(srcdir)$(1) -f $(srcdir)$(1)/Makefile $(2)

all: $(SUBDIRS)
	@(cd $(builddir)lcr; echo ==== `pwd` ===;  $(call sub_make,lcr,all));
	@(cd $(builddir)lib; echo ==== `pwd` ===;  $(call sub_make,lib,all));
	@(cd $(builddir)exec; echo ==== `pwd` ===; $(call sub_make,exec,all));
	@(cd $(builddir)test; echo ==== `pwd` ===; $(call sub_make,test,all));

# subdirs are not phony
.PHONY: all clean install doxygen

$(builddir):
	mkdir -p $@

$(SUBDIRS):
	mkdir -p $@

help:
	@echo 
	@echo "Requirements: GCC, LD, and a Linux 2.4/2.6 kernel."
	@echo "Tested on:"
	@echo " Debian Sarge(i386), Redhat 9(i386), Fedora Core 2 (i386), Fedora Core"
	@echo " 4, 5 (i386,x86_64), SOLARIS, MontaVista Carrier Grade Edition 3.1(i386, x86_64,"
	@echo " classic ppc, ppc970, xscale) and buildroot/uclibc(ppc e500/603e)"
	@echo 
	@echo Targets:
	@echo "  all     - build all targets"
	@echo "  install - install openais onto your system"
	@echo "  clean   - remove generated files"
	@echo "  doxygen - doxygen html docs"
	@echo 
	@echo "Options: (* - default)"
	@echo "  OPENAIS         [DEBUG/RELEASE*] - Enable/Disable debug symbols"
	@echo "  DESTDIR         [directory]      - Install prefix."
	@echo "  O               [directory]      - Locate all output files in \"dir\"."
	@echo "  BUILD_DYNAMIC   [1*/0]           - Enable/disable dynamic loading of service handler modules"
	@echo "  OPENAIS_PROFILE [1/0*]           - Enable profiling"
	@echo 
 

clean:
	(cd $(builddir)lcr; echo ==== `pwd` ===; $(call sub_make,lcr,clean));
	(cd $(builddir)lib; echo ==== `pwd` ===; $(call sub_make,lib,clean));
	(cd $(builddir)exec; echo ==== `pwd` ===; $(call sub_make,exec,clean));
	(cd $(builddir)test; echo ==== `pwd` ===; $(call sub_make,test,clean));
	rm -rf $(builddir)doc/api

AIS_LIBS	= ais SaAmf SaClm SaCkpt SaEvt SaLck SaMsg evs cpg \
		  cfg aisutil confdb

AIS_HEADERS	= saAis.h saAmf.h saClm.h saCkpt.h saEvt.h saEvt.h saLck.h \
		  saMsg.h cpg.h cfg.h evs.h ipc_gen.h mar_gen.h swab.h 	   \
		  ais_util.h confdb.h

EXEC_LIBS	= totem_pg logsys

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


	for eLib in $(EXEC_LIBS); do					\
	    ( cd $(builddir) ;						\
	    ln -sf lib$$eLib.so.2.0.0 exec/lib$$eLib.so;		\
	    ln -sf lib$$eLib.so.2.0.0 exec/lib$$eLib.so.2;		\
	    $(CP) -a exec/lib$$eLib.so $(DESTDIR)$(LIBDIR);		\
	    $(CP) -a exec/lib$$eLib.so.2 $(DESTDIR)$(LIBDIR);		\
	    install -m 755 exec/lib$$eLib.so.2.* $(DESTDIR)$(LIBDIR);	\
	    if [ "xYES" = "x$(STATICLIBS)" ]; then			\
		install -m 755 exec/lib$$eLib.a $(DESTDIR)$(LIBDIR);	\
		if [ ${OPENAIS_COMPAT} = "DARWIN" ]; then		\
		    ranlib $(DESTDIR)$(LIBDIR)/lib$$eLib.a;		\
		fi							\
	    fi								\
	    ) \
	done

	for aLib in $(AIS_LIBS); do					\
	    ( cd $(builddir) ;                                          \
	    ln -sf lib$$aLib.so.2.0.0 lib/lib$$aLib.so;			\
	    ln -sf lib$$aLib.so.2.0.0 lib/lib$$aLib.so.2;		\
	    $(CP) -a lib/lib$$aLib.so $(DESTDIR)$(LIBDIR);		\
	    $(CP) -a lib/lib$$aLib.so.2 $(DESTDIR)$(LIBDIR);		\
	    install -m 755 lib/lib$$aLib.so.2.* $(DESTDIR)$(LIBDIR);	\
	    if [ "xYES" = "x$(STATICLIBS)" ]; then			\
	        install -m 755 lib/lib$$aLib.a $(DESTDIR)$(LIBDIR);	\
		if [ ${OPENAIS_COMPAT} = "DARWIN" ]; then		\
		    ranlib $(DESTDIR)$(LIBDIR)/lib$$aLib.a;		\
	        fi							\
	    fi								\
	    ) \
	done

	echo $(LIBDIR) > "$(DESTDIR)$(ETCDIR)/ld.so.conf.d/openais-$(ARCH).conf"

	install -m 755 $(builddir)exec/*lcrso $(DESTDIR)$(LCRSODIR)
	install -m 755 $(builddir)exec/aisexec $(DESTDIR)$(SBINDIR)
	install -m 700 $(builddir)exec/keygen $(DESTDIR)$(SBINDIR)/ais-keygen

	if [ ! -f $(DESTDIR)$(ETCDIR)/ais/openais.conf ] ; then 	   \
		install -m 644 $(srcdir)conf/openais.conf $(DESTDIR)$(ETCDIR)/ais ; \
	fi
	if [ ! -f $(DESTDIR)$(ETCDIR)/ais/amf.conf ] ; then 		\
		install -m 644 $(srcdir)conf/amf.conf $(DESTDIR)$(ETCDIR)/ais ;	\
	fi

	for aHeader in $(AIS_HEADERS); do				\
	    install -m 644 $(srcdir)include/$$aHeader $(DESTDIR)$(INCLUDEDIR);	\
	done

	install -m 644 $(srcdir)exec/aispoll.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)exec/totempg.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)exec/totem.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)exec/totemip.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)lcr/lcr_ckpt.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 $(srcdir)lcr/lcr_comp.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 $(srcdir)lcr/lcr_ifact.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 $(srcdir)exec/service.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)exec/timer.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)exec/flow.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)exec/ipc.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)exec/objdb.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)exec/logsys.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)exec/config.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)include/swab.h $(DESTDIR)$(INCLUDEDIR_SERVICE)
	install -m 644 $(srcdir)man/*.3 $(DESTDIR)$(MANDIR)/man3
	install -m 644 $(srcdir)man/*.5 $(DESTDIR)$(MANDIR)/man5
	install -m 644 $(srcdir)man/*.8 $(DESTDIR)$(MANDIR)/man8

doxygen:
	mkdir -p doc/api && doxygen
