# Copyright (c) 2012-2016 Red Hat, Inc.
#
# Authors: Fabio M. Di Nitto (fdinitto@redhat.com)
#          Jan Friesse (jfriesse@redhat.com)
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

sbin_PROGRAMS		= corosync-qnetd corosync-qdevice

sbin_SCRIPTS             = corosync-qnetd-certutil

corosync_qnetd_SOURCES	= corosync-qnetd.c dynar.c msg.c msgio.c nss-sock.c  \
			    qnetd-client.c qnetd-client-list.c qnetd-log.c \
			    qnetd-poll-array.c timer-list.c tlv.c send-buffer-list.c \
			    node-list.c qnetd-algo-test.c qnetd-algorithm.c qnetd-algo-utils.c \
			    qnetd-algo-ffsplit.c qnetd-cluster-list.c qnetd-client-send.c \
			    qnetd-algo-2nodelms.c qnetd-algo-lms.c utils.c qnetd-instance.c \
			    qnetd-client-net.c qnetd-client-msg-received.c qnetd-log-debug.c \
			    qnetd-client-algo-timer.c qnetd-dpd-timer.c

corosync_qdevice_SOURCES	= corosync-qdevice.c qdevice-cmap.c qdevice-instance.c node-list.c \
				  utils.c qdevice-log.c qdevice-log-debug.c qdevice-votequorum.c \
				  qdevice-model.c \
				  qdevice-model-net.c \
				  qdevice-net-instance.c dynar.c send-buffer-list.c timer-list.c \
				  msg.c msgio.c nss-sock.c tlv.c unix-socket.c qdevice-local-socket.c \
				  qdevice-net-poll.c qdevice-net-send.c qdevice-net-votequorum.c \
				  qdevice-net-socket.c qdevice-net-nss.c qdevice-net-msg-received.c \
				  qdevice-net-cast-vote-timer.c qdevice-net-echo-request-timer.c \
				  qdevice-net-algorithm.c \
				  qdevice-net-algo-test.c qdevice-net-algo-ffsplit.c \
				  qdevice-net-algo-2nodelms.c qdevice-net-algo-lms.c

corosync_qnetd_CFLAGS		= $(nss_CFLAGS)
corosync_qnetd_LDADD		= $(nss_LIBS)

corosync_qdevice_CFLAGS	= $(nss_CFLAGS)
corosync_qdevice_LDADD	= $(nss_LIBS) $(LIBQB_LIBS) $(top_builddir)/lib/libcmap.la \
				    $(top_builddir)/lib/libvotequorum.la

corosync-qnetd-certutil: corosync-qnetd-certutil.sh
	sed -e 's#@''DATADIR@#${datadir}#g' \
	    -e 's#@''BASHPATH@#${BASHPATH}#g' \
	    -e 's#@''COROSYSCONFDIR@#${COROSYSCONFDIR}#g' \
	    $< > $@

TESTS				= qnetd-cluster-list.test
check_PROGRAMS			= qnetd-cluster-list.test

qnetd_cluster_list_test_SOURCES	= qnetd-cluster-list.c test-qnetd-cluster-list.c \
				    qnetd-client-list.c qnetd-client.c dynar.c node-list.c \
				    send-buffer-list.c
qnetd_cluster_list_test_CFLAGS  = $(nss_CFLAGS)
qnetd_cluster_list_test_LDADD	= $(nss_LIBS)

endif
