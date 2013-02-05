# Copyright (c) 2013 Red Hat, Inc.
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
			  corosync.conf.example.udpu \
			  corosync.xml.example \
			  xml2conf.xsl \
			  lenses/corosync.aug \
			  lenses/tests/test_corosync.aug

corosysconfdir		= ${COROSYSCONFDIR}

corosysconf_DATA	= corosync.conf.example \
			  corosync.conf.example.udpu

if INSTALL_AUGEAS
corolensdir		= ${datadir}/augeas/lenses
corolens_DATA		= lenses/corosync.aug

corolenstestdir		= ${corolensdir}/tests
corolenstest_DATA	= lenses/tests/test_corosync.aug
endif

if INSTALL_XMLCONF
corosysxmlconfdir	= ${COROSYSCONFDIR}
corosysxmlconf_DATA	= corosync.xml.example

corosysxmlxsltdir	= ${datadir}/corosync
corosysxmlxslt_DATA	= xml2conf.xsl
endif

if INSTALL_MIB
mibdir			= $(datadir)/snmp/mibs
mib_DATA		= COROSYNC-MIB.txt
endif

if INSTALL_DBUSCONF
dbusdir			= $(sysconfdir)/dbus-1/system.d
dbus_DATA		= corosync-signals.conf
endif

#
# rnc -> man page DocBook, Augeas lens
#

CONF_NAME		= corosync.conf
CONF_SCHEMA_ORIG	= corosync.rnc
CONF_XSL_ASCIIDOCMAN	= rng2asciidocman.xsl
CONF_XSL_LENS		= rng2lens.xsl
CONF_XSLS_ASCIIDOCMAN	= $(CONF_XSL_ASCIIDOCMAN) common.xsl
CONF_XSLS_LENS		= $(CONF_XSL_LENS) xsddatatypes2lens.xsl common.xsl
CONF_SCRIPT		= rnctree.py
CONF_SIMPLIFICATOR	= simplification-wrapper.xsl
# derived
CONF_ASCIIDOC		= $(CONF_NAME:=.asciidoc)  # fixed name
CONF_DOCBOOK		= $(CONF_NAME:=.xml)       # fixed name

CONF_SCHEMA		= $(CONF_SCHEMA_ORIG:.rnc=.rng)
CONF_SCHEMA_ORIG_RNG	= $(CONF_SCHEMA_ORIG:.rnc=.orig.rng)

noinst_DATA		= $(CONF_SCHEMA_ORIG) $(CONF_SCHEMA_ORIG_RNG) \
			  $(CONF_ASCIIDOC) $(CONF_DOCBOOK)

$(CONF_DOCBOOK): $(CONF_ASCIIDOC)
	$(ASCIIDOC) --doctype=manpage -b docbook -d manpage $<

$(CONF_ASCIIDOC): $(CONF_SCHEMA) $(CONF_XSLS_ASCIIDOCMAN)
	$(XSLTPROC) $(CONF_XSL_ASCIIDOCMAN) $< > $@

lenses/corosync.aug: $(CONF_SCHEMA_ORIG_RNG) $(CONF_XSLS_LENS)
	$(XSLTPROC) $(CONF_XSL_LENS) $< > $@

#trang -O rng $(CONF_SCHEMA_ORIG) $@
$(CONF_SCHEMA_ORIG_RNG): $(CONF_SCHEMA_ORIG) $(CONF_SCRIPT)
	cat $< | $(PYTHON) $(CONF_SCRIPT) > $@

$(CONF_SCHEMA): $(CONF_SCHEMA_ORIG_RNG) $(CONF_SIMPLIFICATOR)
	$(XSLTPROC) $(CONF_SIMPLIFICATOR) $< > $@

all-local: lenses/corosync.aug
