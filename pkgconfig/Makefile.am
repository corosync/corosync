# Copyright (c) 2009 Red Hat, Inc.
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

MAINTAINERCLEANFILES	= Makefile.in

LIBS	= cfg confdb coroipcc coroipcs cpg evs logsys pload quorum \
	  totem_pg votequorum

pkgconf_LIBS = $(LIBS:%=lib%)

target_LIBS = $(LIBS:%=lib%.pc)

target_PACKAGE = corosync.pc

lib%:
	cat $(srcdir)/libtemplate.pc.in | sed \
		-e 's#@PREFIX@#$(exec_prefix)#g' \
		-e 's#@LIBDIR@#$(libdir)#g' \
		-e 's#@LIBVERSION@#$(VERSION)#g' \
		-e 's#@LIB@#'$@'#g' \
	> $@.pc;
	touch $@

$(target_PACKAGE):
	cat $(srcdir)/$@.in | sed \
		-e 's#@PREFIX@#$(exec_prefix)#g' \
		-e 's#@LIBDIR@#$(libdir)#g' \
		-e 's#@LIBVERSION@#$(VERSION)#g' \
		-e 's#@COROLCRSODIR@#$(LCRSODIR)#g' \
		-e 's#@COROSOCKETDIR@#$(SOCKETDIR)#g' \
	> $@

all-local: $(pkgconf_LIBS) $(target_PACKAGE)

install-exec-local:
	install -d $(DESTDIR)/$(libdir)/pkgconfig
	install -m 644 $(target_LIBS) $(target_PACKAGE) $(DESTDIR)/$(libdir)/pkgconfig

uninstall-local:
	cd $(DESTDIR)/$(libdir)/pkgconfig && rm -f $(target_LIBS) $(target_PACKAGE)
	rmdir $(DESTDIR)/$(libdir)/pkgconfig 2> /dev/null || :

clean-local:
	rm -f *.pc $(pkgconf_LIBS)
