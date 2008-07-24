# Copyright (c) 2002-2006 MontaVista Software, Inc.
# Copyright (c) 2006-2008 Red Hat, Inc.
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
# SUBSTITUTE GOODS OR ENGINES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

builddir:=$(shell pwd)/
ifneq ($(O),)
# cleanup the path (make it absolute)
$(shell mkdir -p $(O))
builddir:=$(shell cd $(O) && pwd)/
endif
srcdir:=$(shell cd $(dir $(MAKEFILE_LIST)) && pwd)/

include $(srcdir)/Makefile.inc

SBINDIR=$(PREFIX)/sbin
INCLUDEDIR=$(PREFIX)/include/corosync
INCLUDEDIR_TOTEM=$(PREFIX)/include/corosync/totem
INCLUDEDIR_LCR=$(PREFIX)/include/corosync/lcr
INCLUDEDIR_ENGINE=$(PREFIX)/include/corosync/engine
MANDIR=$(PREFIX)/share/man
ETCDIR=/etc
ARCH=$(shell uname -p)

ifeq (,$(findstring 64,$(ARCH)))
LIBDIR=$(PREFIX)/lib/corosync
else
LIBDIR=$(PREFIX)/lib64/corosync
endif
ifeq (s390,$(ARCH))
LIBDIR=$(PREFIX)/lib/corosync
endif
ifeq (s390x,$(ARCH))
LIBDIR=$(PREFIX)/lib64/corosync
endif
ifeq (ia64,$(ARCH))
LIBDIR=$(PREFIX)/lib/corosync
endif

SUBDIRS:=$(builddir)lcr $(builddir)lib $(builddir)exec $(builddir)test $(builddir)services
sub_make = srcdir=$(srcdir) builddir=$(builddir) subdir=$(1)/ $(MAKE) -I$(srcdir)$(1) -f $(srcdir)$(1)/Makefile $(2)

all: $(SUBDIRS)
	@(cd $(builddir)lcr; echo ==== `pwd` ===;  $(call sub_make,lcr,all));
	@(cd $(builddir)lib; echo ==== `pwd` ===;  $(call sub_make,lib,all));
	@(cd $(builddir)exec; echo ==== `pwd` ===; $(call sub_make,exec,all));
	@(cd $(builddir)services; echo ==== `pwd` ===; $(call sub_make,services,all));
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
	@echo "  install - install corosync onto your system"
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
	(cd $(builddir)services; echo ==== `pwd` ===; $(call sub_make,services,clean));
	(cd $(builddir)test; echo ==== `pwd` ===; $(call sub_make,test,clean));
	rm -rf $(builddir)doc/api

AIS_LIBS	= evs cpg cfg coroutil confdb

AIS_HEADERS	= cpg.h cfg.h evs.h ipc_gen.h mar_gen.h swab.h \
		  ais_util.h confdb.h list.h

EXEC_LIBS	= totem_pg logsys

install: all
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_TOTEM)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_LCR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR_ENGINE)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(LCRSODIR)
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

	echo $(LIBDIR) > "$(DESTDIR)$(ETCDIR)/ld.so.conf.d/corosync-$(ARCH).conf"

	install -m 755 $(builddir)exec/*lcrso $(DESTDIR)$(LCRSODIR)
	install -m 755 $(builddir)services/*lcrso $(DESTDIR)$(LCRSODIR)
	install -m 755 $(builddir)exec/corosync $(DESTDIR)$(SBINDIR)
	install -m 700 $(builddir)exec/keygen $(DESTDIR)$(SBINDIR)/ais-keygen

	if [ ! -f $(DESTDIR)$(ETCDIR)/penais.conf ] ; then 	   \
		install -m 644 $(srcdir)conf/corosync.conf $(DESTDIR)$(ETCDIR) ; \
	fi

	for aHeader in $(AIS_HEADERS); do				\
	    install -m 644 $(srcdir)include/$$aHeader $(DESTDIR)$(INCLUDEDIR);	\
	done

	install -m 644 $(srcdir)exec/coropoll.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)exec/totempg.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)exec/totem.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)exec/totemip.h $(DESTDIR)$(INCLUDEDIR_TOTEM)
	install -m 644 $(srcdir)lcr/lcr_ckpt.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 $(srcdir)lcr/lcr_comp.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 $(srcdir)lcr/lcr_ifact.h $(DESTDIR)$(INCLUDEDIR_LCR)
	install -m 644 $(srcdir)include/coroapi.h $(DESTDIR)$(INCLUDEDIR_ENGINE)
	install -m 644 $(srcdir)exec/objdb.h $(DESTDIR)$(INCLUDEDIR_ENGINE)
	install -m 644 $(srcdir)exec/logsys.h $(DESTDIR)$(INCLUDEDIR_ENGINE)
	install -m 644 $(srcdir)exec/config.h $(DESTDIR)$(INCLUDEDIR_ENGINE)
	install -m 644 $(srcdir)include/swab.h $(DESTDIR)$(INCLUDEDIR_ENGINE)
	install -m 644 $(srcdir)man/*.3 $(DESTDIR)$(MANDIR)/man3
	install -m 644 $(srcdir)man/*.5 $(DESTDIR)$(MANDIR)/man5
	install -m 644 $(srcdir)man/*.8 $(DESTDIR)$(MANDIR)/man8

doxygen:
	mkdir -p doc/api && doxygen
