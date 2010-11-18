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
MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS		= -fPIC

INCLUDES 		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= liblcr.a

noinst_PROGRAMS		= test test_static uic

noinst_LCRSO		= libtest_a.lcrso libtest_b.lcrso

uic_SOURCES		= uic.c

liblcr_a_SOURCES	= lcr_ifact.c

test_SOURCES		= test.c uis.c
test_LDADD		= liblcr.a

test_static_SOURCES	= test.c libtest_a.c libtest_b.c uis.c lcr_ifact.c

if BUILD_DARWIN

%.lcrso: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) -bundle -bundle_loader ./test $^ -o $@
else

if BUILD_SOLARIS

%.lcrso: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) -shared $^ -o $@
else

%.lcrso: %.o
	$(CC) $(LDFLAGS) $(CFLAGS) -shared -Wl,-soname,$@ $^ -o $@

endif

endif

lint:
	-splint $(LINT_FLAGS) $(CFLAGS) *.c

all-local: $(noinst_LCRSO)
	@echo Built Live Component Replacement System

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg *.lcrso
