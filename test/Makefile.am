#
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
INCLUDES       		= -I$(top_builddir)/include/corosync -I$(top_srcdir)/include

EXTRA_DIST		= ploadstart.sh

noinst_PROGRAMS		= cpgverify testcpg testcpg2 cpgbench \
			  testquorum testvotequorum1 testvotequorum2	\
			  stress_cpgfdget stress_cpgcontext cpgbound testsam \
			  testcpgzc cpgbenchzc testzcgc stress_cpgzc

noinst_SCRIPTS		= ploadstart

COMMON_OPTS		= -L../lib -L../common_lib -Wl,-rpath,../common_lib

testcpg_LDADD		= -lcpg $(LIBQB_LIBS)
testcpg_LDFLAGS		= $(COMMON_OPTS)
testcpg2_LDADD		= -lcpg $(LIBQB_LIBS)
testcpg2_LDFLAGS	= $(COMMON_OPTS)
testcpgzc_LDADD		= -lcpg $(LIBQB_LIBS)
testcpgzc_LDFLAGS	= $(COMMON_OPTS)
testzcgc_LDADD		= -lcpg $(LIBQB_LIBS)
testzcgc_LDFLAGS	= $(COMMON_OPTS)
stress_cpgzc_LDADD	= -lcpg $(LIBQB_LIBS)
stress_cpgzc_LDFLAGS	= $(COMMON_OPTS)
stress_cpgfdget_LDADD	= -lcpg $(LIBQB_LIBS)
stress_cpgfdget_LDFLAGS	= $(COMMON_OPTS)
stress_cpgcontext_LDADD	= -lcpg $(LIBQB_LIBS)
stress_cpgcontext_LDFLAGS	= $(COMMON_OPTS)
testquorum_LDADD	= -lquorum $(LIBQB_LIBS)
testquorum_LDFLAGS	= $(COMMON_OPTS)
testvotequorum1_LDADD	= -lvotequorum $(LIBQB_LIBS)
testvotequorum1_LDFLAGS	= $(COMMON_OPTS)
testvotequorum2_LDADD	= -lvotequorum $(LIBQB_LIBS)
testvotequorum2_LDFLAGS	= $(COMMON_OPTS)
cpgverify_CFLAGS	= $(nss_CFLAGS)
cpgverify_LDADD		= -lcpg -ltotem_pg $(LIBQB_LIBS) $(nss_LIBS)
cpgverify_LDFLAGS	= $(COMMON_OPTS) -L../exec
cpgbound_LDADD		= -lcpg $(LIBQB_LIBS)
cpgbound_LDFLAGS	= $(COMMON_OPTS)
cpgbench_LDADD		= -lcpg $(LIBQB_LIBS)
cpgbench_LDFLAGS	= $(COMMON_OPTS)
cpgbenchzc_LDADD	= -lcpg -lcorosync_common $(LIBQB_LIBS)
cpgbenchzc_LDFLAGS	= $(COMMON_OPTS)
testsam_LDADD		= -lsam -lcmap -lquorum $(LIBQB_LIBS)
testsam_LDFLAGS		= $(COMMON_OPTS)

ploadstart: ploadstart.sh
	cp $^ $@
	chmod 755 $@

LINT_FILES1:=$(filter-out sa_error.c, $(wildcard *.c))
LINT_FILES:=$(filter-out testparse.c, $(LINT_FILES1))

lint:
	-for f in $(LINT_FILES) ; do echo Splint $$f ; splint $(INCLUDES) $(LINT_FLAGS) $(CFLAGS) $$f ; done

clean-local:
	rm -f fdata ploadstart
