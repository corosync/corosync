# Copyright (c) 2009 Red Hat, Inc.
#
# Authors: Andrew Beekhof
#         Steven Dake (sdake@redhat.com)
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

EXTRA_DIST		= COROSYNC-MIB.txt corosync-signals.conf \
			  corosync.conf.example \
			  xml2conf.xsl \
			  lenses/corosync.aug \
			  lenses/tests/test_corosync.aug

corosysconfdir		= ${COROSYSCONFDIR}

corosysconf_DATA	= corosync.conf.example

if INSTALL_AUGEAS
corolensdir		= ${datadir}/augeas/lenses
corolens_DATA		= lenses/corosync.aug

corolenstestdir		= ${corolensdir}/tests
corolenstest_DATA	= lenses/tests/test_corosync.aug
endif

if INSTALL_XMLCONF
corosysxmlxsltdir	= ${datadir}/corosync
corosysxmlxslt_DATA	= xml2conf.xsl
endif

if INSTALL_MIB
mibdir			= $(datadir)/snmp/mibs
mib_DATA		= COROSYNC-MIB.txt
endif

if INSTALL_DBUSCONF
dbusdir			= $(datadir)/dbus-1/system.d
dbus_DATA		= corosync-signals.conf
endif

SUBDIRS = logrotate
