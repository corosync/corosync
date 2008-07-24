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

INCLUDEDIR=$(PREFIX)/include/openais
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
	@(cd $(builddir)lib; echo ==== `pwd` ===;  $(call sub_make,lib,all));
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
	(cd $(builddir)lib; echo ==== `pwd` ===; $(call sub_make,lib,clean));
	(cd $(builddir)services; echo ==== `pwd` ===; $(call sub_make,services,clean));
	(cd $(builddir)test; echo ==== `pwd` ===; $(call sub_make,test,clean));
	rm -rf $(builddir)doc/api

AIS_LIBS	= SaAmf SaClm SaCkpt SaEvt SaLck SaMsg

AIS_HEADERS	= saAis.h saAmf.h saClm.h saCkpt.h saEvt.h saEvt.h saLck.h \
		  saMsg.h

install: all
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(LCRSODIR)
	mkdir -p $(DESTDIR)$(ETCDIR)/ais
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	mkdir -p $(DESTDIR)$(ETCDIR)/ld.so.conf.d

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

	install -m 755 $(builddir)services/*lcrso $(DESTDIR)$(LCRSODIR)

	if [ ! -f $(DESTDIR)$(ETCDIR)/ais/openais.conf ] ; then 	   \
		install -m 644 $(srcdir)conf/openais.conf $(DESTDIR)$(ETCDIR)/ais ; \
	fi
	if [ ! -f $(DESTDIR)$(ETCDIR)/ais/amf.conf ] ; then 		\
		install -m 644 $(srcdir)conf/amf.conf $(DESTDIR)$(ETCDIR)/ais ;	\
	fi

	for aHeader in $(AIS_HEADERS); do				\
	    install -m 644 $(srcdir)include/$$aHeader $(DESTDIR)$(INCLUDEDIR);	\
	done

doxygen:
	mkdir -p doc/api && doxygen
