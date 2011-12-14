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

noinst_PROGRAMS		= testevs evsbench evsverify cpgverify testcpg testcpg2 cpgbench \
			testquorum testvotequorum1 testvotequorum2	\
			stress_cpgfdget stress_cpgcontext cpgbound testsam \
			testcpgzc cpgbenchzc testzcgc stress_cpgzc \
			logsys_s logsys_t1 logsys_t2

testevs_LDADD		= -levs $(LIBQB_LIBS)
testevs_LDFLAGS		= -L../lib
testcpg_LDADD		= -lcpg $(LIBQB_LIBS)
testcpg_LDFLAGS		= -L../lib
testcpg2_LDADD		= -lcpg $(LIBQB_LIBS)
testcpg2_LDFLAGS	= -L../lib
testcpgzc_LDADD		= -lcpg $(LIBQB_LIBS)
testcpgzc_LDFLAGS	= -L../lib
testzcgc_LDADD		= -lcpg $(LIBQB_LIBS)
testzcgc_LDFLAGS	= -L../lib
stress_cpgzc_LDADD	= -lcpg $(LIBQB_LIBS)
stress_cpgzc_LDFLAGS	= -L../lib
stress_cpgfdget_LDADD	= -lcpg $(LIBQB_LIBS)
stress_cpgfdget_LDFLAGS	= -L../lib
stress_cpgcontext_LDADD	= -lcpg $(LIBQB_LIBS)
stress_cpgcontext_LDFLAGS	= -L../lib
testquorum_LDADD	= -lquorum $(LIBQB_LIBS)
testquorum_LDFLAGS	= -L../lib
testvotequorum1_LDADD	= -lvotequorum $(LIBQB_LIBS)
testvotequorum1_LDFLAGS	= -L../lib
testvotequorum2_LDADD	= -lvotequorum $(LIBQB_LIBS)
testvotequorum2_LDFLAGS	= -L../lib
evsverify_LDADD		= -levs -ltotem_pg $(LIBQB_LIBS)
evsverify_LDFLAGS	= -L../lib -L../exec
cpgverify_LDADD		= -lcpg -ltotem_pg $(LIBQB_LIBS)
cpgverify_LDFLAGS	= -L../lib -L../exec
cpgbound_LDADD		= -lcpg $(LIBQB_LIBS)
cpgbound_LDFLAGS	= -L../lib
evsbench_LDADD		= -levs $(LIBQB_LIBS)
evsbench_LDFLAGS	= -L../lib
cpgbench_LDADD		= -lcpg $(LIBQB_LIBS)
cpgbench_LDFLAGS	= -L../lib
cpgbenchzc_LDADD	= -lcpg $(LIBQB_LIBS)
cpgbenchzc_LDFLAGS	= -L../lib
logsys_s_SOURCES	= logsys_s.c logsys_s1.c logsys_s2.c
logsys_s_LDADD		= -llogsys  $(LIBQB_LIBS)
logsys_s_LDFLAGS	= -L../exec
logsys_t1_LDADD		= -llogsys  $(LIBQB_LIBS)
logsys_t1_LDFLAGS	= -L../exec
logsys_t2_LDADD		= -llogsys  $(LIBQB_LIBS)
logsys_t2_LDFLAGS	= -L../exec
testsam_LDADD		= -lsam -lcmap -lquorum $(LIBQB_LIBS)
testsam_LDFLAGS		= -L../lib

LINT_FILES1:=$(filter-out sa_error.c, $(wildcard *.c))
LINT_FILES2:=$(filter-out testevsth.c, $(LINT_FILES1))
LINT_FILES:=$(filter-out testparse.c, $(LINT_FILES2))


lint:
	-for f in $(LINT_FILES) ; do echo Splint $$f ; splint $(INCLUDES) $(LINT_FLAGS) $(CFLAGS) $$f ; done

clean-local:
	rm -f fdata
