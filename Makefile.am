# Copyright (c) 2009 Red Hat, Inc.
# 
# Authors: Andrew Beekhof
#	   Steven Dake (sdake@redhat.com)
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

EXTRA_DIST              = autogen.sh
AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf

ETCDIR = /etc
MANDIR = /usr/share/man
SUBDIRS	= include lcr lib exec services tools test pkgconfig
# man init conf

lint:
	for dir in lcr lib exec services tools test; do make -C $$dir lint; done

doxygen:
	mkdir -p doc/api && doxygen

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(ETCDIR)
	if [ ! -f $(DESTDIR)$(ETCDIR)/corosync.conf ] ; then 	   \
		$(INSTALL) -m 644 $(srcdir)/conf/corosync.conf $(DESTDIR)$(ETCDIR) ; \
	fi
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	$(INSTALL) -m 644 $(srcdir)/man/*.3 $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	$(INSTALL) -m 644 $(srcdir)/man/*.5 $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) -m 644 $(srcdir)/man/*.8 $(DESTDIR)$(MANDIR)/man8

dist-clean-local:
	rm -f autoconf automake autoheader $(TARFILE)

clean-generic:
	rm -rf doc/api
