# Copyright (c) 2012 Red Hat, Inc.
#
# Authors: Fabio M. Di Nitto <fdinitto@redhat.com>
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

if BUILD_QDEVICES

SUBDIRS			=

sbin_PROGRAMS		= corosync-qnetd corosync-qdevice-net

sbin_SCRIPTS             = corosync-qnetd-certutil

corosync_qnetd_SOURCES	= corosync-qnetd.c dynar.c msg.c msgio.c nss-sock.c  \
			    qnetd-client.c qnetd-client-list.c qnetd-log.c \
			    qnetd-poll-array.c timer-list.c tlv.c send-buffer-list.c \
			    node-list.c

corosync_qdevice_net_SOURCES	= corosync-qdevice-net.c dynar.c msg.c msgio.c nss-sock.c  \
			    qnetd-client.c qnetd-client-list.c qnetd-log.c \
			    qnetd-poll-array.c timer-list.c tlv.c send-buffer-list.c \
			    node-list.c

corosync_qnetd_CFLAGS		= $(nss_CFLAGS)
corosync_qnetd_LDADD		= $(nss_LIBS)

corosync_qdevice_net_CFLAGS	= $(nss_CFLAGS)
corosync_qdevice_net_LDADD	= $(nss_LIBS) $(LIBQB_LIBS) $(top_builddir)/lib/libcmap.la \
				    $(top_builddir)/lib/libvotequorum.la

corosync-qnetd-certutil: corosync-qnetd-certutil.sh
	sed -e 's#@''DATADIR@#${datadir}#g' \
	    -e 's#@''BASHPATH@#${BASHPATH}#g' \
	    $< > $@

endif
