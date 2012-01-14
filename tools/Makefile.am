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
INCLUDES       		= -I$(top_builddir)/include  -I$(top_srcdir)/include \
                          -I$(top_builddir)/include/corosync

sbin_PROGRAMS		= corosync-fplay corosync-cfgtool \
			  corosync-keygen \
			  corosync-pload corosync-cpgtool corosync-quorumtool \
			  corosync-notifyd corosync-cmapctl

bin_SCRIPTS		= corosync-blackbox

if INSTALL_XMLCONF
bin_SCRIPTS		+= corosync-xmlproc
endif

corosync-xmlproc: corosync-xmlproc.sh
	sed -e 's#@''DATADIR@#${datadir}#g' $< > $@

EXTRA_DIST		= $(bin_SCRIPTS) corosync-xmlproc.sh corosync-notifyd.sysconfig.example

corosync_fplay_LDADD	= $(LIBQB_LIBS)
corosync_pload_LDADD	= -lpload $(LIBQB_LIBS)
corosync_pload_LDFLAGS	= -L../lib
corosync_cmapctl_LDADD	= -lcmap $(LIBQB_LIBS)
corosync_cmapctl_LDFLAGS= -L../lib
corosync_cfgtool_LDADD	= -lcfg $(LIBQB_LIBS)
corosync_cfgtool_LDFLAGS= -L../lib
corosync_cpgtool_LDADD	= -lcfg -lcpg $(LIBQB_LIBS)
corosync_cpgtool_LDFLAGS= -L../lib
corosync_quorumtool_LDADD = -lcmap -lcfg -lquorum \
			    -lvotequorum $(LIBQB_LIBS)
corosync_quorumtool_LDFLAGS = -L../lib

corosync_notifyd_LDADD = -lcfg -lcmap \
			   $(LIBQB_LIBS) $(DBUS_LIBS) $(SNMPLIBS) \
			   -lquorum
corosync_notifyd_LDFLAGS = -L../lib
corosync_notifyd_CPPFLAGS = $(DBUS_CFLAGS)


lint:
	-splint $(LINT_FLAGS) $(DBUS_CFLAGS) $(INCLUDES) $(CFLAGS) *.c

clean-local:
	rm -f corosync-xmlproc

