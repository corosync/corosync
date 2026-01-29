#
# Copyright (C) 2026 Red Hat, Inc.  All rights reserved.
#
# Author: Fabio M. Di Nitto <fabbione@kronosnet.org>
#
# This software licensed under GPL-2.0+
#

MAINTAINERCLEANFILES		= Makefile.in

include $(top_srcdir)/build-aux/check.mk

noinst_HEADERS			= packet-corosync-totemknet.h

wsdissectorlibdir		= $(wireshark_INSTALL_PREFIX)/$(wireshark_PLUGINDIR)/epan

wsdissectorlib_LTLIBRARIES	= corosync-totemknet.la

# override global LIBS that pulls in lots of craft we don't need here
LIBS				=

corosync_totemknet_la_SOURCES		= packet-corosync-totemknet.c

corosync_totemknet_la_CFLAGS		= $(wireshark_CFLAGS) -DPLUGINPATH="\"$(pkglibdir)\"" -DWIRESHARK_BUILD=1
corosync_totemknet_la_LDFLAGS		= -module -avoid-version -export-dynamic
corosync_totemknet_la_LIBADD		= $(wireshark_LIBS)
