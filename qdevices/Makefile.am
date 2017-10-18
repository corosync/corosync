# Copyright (c) 2012-2017 Red Hat, Inc.
#
# Authors: Jan Friesse (jfriesse@redhat.com)
#          Fabio M. Di Nitto (fdinitto@redhat.com)
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

SUBDIRS			=

bin_PROGRAMS		=
sbin_PROGRAMS		=
bin_SCRIPTS		=
sbin_SCRIPTS		=
EXTRA_DIST		= corosync-qnetd-certutil.sh corosync-qdevice-net-certutil.sh

if BUILD_QNETD

bin_PROGRAMS		+= corosync-qnetd corosync-qnetd-tool

bin_SCRIPTS             += corosync-qnetd-certutil

corosync_qnetd_SOURCES	= corosync-qnetd.c \
                          dynar.c dynar.h msg.c msg.h msgio.c msgio.h \
                          nss-sock.c nss-sock.h qnetd-client.c qnetd-client.h \
                          qnetd-client-list.c qnetd-client-list.h qnetd-log.c qnetd-log.h \
                          pr-poll-array.c pr-poll-array.h timer-list.c timer-list.h tlv.c tlv.h \
                          send-buffer-list.c send-buffer-list.h node-list.c node-list.h \
                          qnetd-algo-test.c qnetd-algo-test.h qnetd-algorithm.c qnetd-algorithm.h \
                          qnetd-algo-utils.c qnetd-algo-utils.h \
                          qnetd-algo-ffsplit.c qnetd-algo-ffsplit.h \
                          qnetd-cluster.c qnetd-cluster.h \
                          qnetd-cluster-list.c qnetd-cluster-list.h \
                          qnetd-client-send.c qnetd-client-send.h \
                          qnetd-algo-2nodelms.c qnetd-algo-2nodelms.h qnetd-algo-lms.c qnetd-algo-lms.h \
                          utils.c utils.h qnetd-instance.c qnetd-instance.h \
                          qnetd-client-net.c qnetd-client-net.h \
                          qnetd-client-msg-received.c qnetd-client-msg-received.h \
                          qnetd-log-debug.c qnetd-log-debug.h \
                          qnetd-client-algo-timer.c qnetd-client-algo-timer.h \
                          qnetd-dpd-timer.c qnetd-dpd-timer.h \
                          qnetd-ipc.c qnetd-ipc.h unix-socket-ipc.c unix-socket-ipc.h \
                          dynar-simple-lex.c dynar-simple-lex.h dynar-str.c dynar-str.h \
                          unix-socket-client.c unix-socket-client.h \
                          unix-socket-client-list.c unix-socket-client-list.h \
                          unix-socket.c unix-socket.h qnetd-ipc-cmd.c qnetd-ipc-cmd.h \
                          qnetd-poll-array-user-data.h qnet-config.h dynar-getopt-lex.c \
                          dynar-getopt-lex.h qnetd-advanced-settings.c qnetd-advanced-settings.h

corosync_qnetd_tool_SOURCES = corosync-qnetd-tool.c unix-socket.c unix-socket.h dynar.c dynar.h \
                              dynar-str.c dynar-str.h utils.c utils.h

corosync_qnetd_CFLAGS		= $(nss_CFLAGS)
corosync_qnetd_LDADD		= $(nss_LIBS)

corosync-qnetd-certutil: corosync-qnetd-certutil.sh
	sed -e 's#@''DATADIR@#${datadir}#g' \
	    -e 's#@''BASHPATH@#${BASHPATH}#g' \
	    -e 's#@''COROSYSCONFDIR@#${COROSYSCONFDIR}#g' \
	    $< > $@

endif

if BUILD_QDEVICES

sbin_PROGRAMS		+= corosync-qdevice corosync-qdevice-tool

sbin_SCRIPTS            += corosync-qdevice-net-certutil

corosync_qdevice_SOURCES = corosync-qdevice.c \
                           qdevice-cmap.c qdevice-cmap.h \
                           qdevice-instance.c qdevice-instance.h node-list.c node-list.h \
                           utils.c utils.h qdevice-log.c qdevice-log.h \
                           qdevice-log-debug.c qdevice-log-debug.h \
                           qdevice-votequorum.c qdevice-votequorum.h \
                           qdevice-model.c qdevice-model.h qdevice-model-net.c qdevice-model-net.h \
                           qdevice-net-instance.c qdevice-net-instance.h dynar.c dynar.h \
                           send-buffer-list.c send-buffer-list.h timer-list.c timer-list.h \
                           msg.c msg.h msgio.c msgio.h nss-sock.c nss-sock.h tlv.c tlv.h \
                           unix-socket.c unix-socket.h unix-socket-client.c unix-socket-client.h \
                           unix-socket-client-list.c unix-socket-client-list.h \
                           unix-socket-ipc.c unix-socket-ipc.h qdevice-ipc.c qdevice-ipc.h \
                           pr-poll-array.c pr-poll-array.h dynar-simple-lex.c dynar-simple-lex.h \
                           dynar-str.c dynar-str.h qdevice-ipc-cmd.c qdevice-ipc-cmd.h \
                           qdevice-net-ipc-cmd.c qdevice-net-ipc-cmd.h \
                           qdevice-net-poll.c qdevice-net-poll.h \
                           qdevice-net-send.c qdevice-net-send.h \
                           qdevice-net-votequorum.c qdevice-net-votequorum.h \
                           qdevice-net-socket.c qdevice-net-socket.h \
                           qdevice-net-nss.c qdevice-net-nss.h \
                           qdevice-net-msg-received.c qdevice-net-msg-received.h \
                           qdevice-net-cast-vote-timer.c qdevice-net-cast-vote-timer.h \
                           qdevice-net-echo-request-timer.c qdevice-net-echo-request-timer.h \
                           qdevice-net-algorithm.c qdevice-net-algorithm.h \
                           qdevice-net-algo-test.c qdevice-net-algo-test.h \
                           qdevice-net-algo-ffsplit.c qdevice-net-algo-ffsplit.h \
                           qdevice-net-algo-2nodelms.c qdevice-net-algo-2nodelms.h \
                           qdevice-net-algo-lms.c qdevice-net-algo-lms.h \
                           qdevice-net-poll-array-user-data.h \
                           qdevice-config.h qnet-config.h qdevice-net-disconnect-reason.h \
                           qdevice-model-type.h qdevice-advanced-settings.c \
                           qdevice-advanced-settings.h dynar-getopt-lex.c dynar-getopt-lex.h \
                           qdevice-heuristics.h qdevice-heuristics.c \
                           qdevice-heuristics-worker.h qdevice-heuristics-worker.c \
                           qdevice-heuristics-io.h qdevice-heuristics-io.c \
                           qdevice-heuristics-worker-instance.h \
                           qdevice-heuristics-worker-log.h qdevice-heuristics-worker-log.c \
                           qdevice-heuristics-log.h qdevice-heuristics-log.c \
                           qdevice-heuristics-instance.h qdevice-heuristics-instance.c \
                           qdevice-heuristics-mode.h qdevice-heuristics-mode.c \
                           qdevice-heuristics-exec-list.c qdevice-heuristics-exec-list.h \
                           qdevice-heuristics-cmd.c qdevice-heuristics-cmd.h \
                           qdevice-heuristics-worker-cmd.c qdevice-heuristics-worker-cmd.h \
                           qdevice-heuristics-cmd-str.h \
                           qdevice-heuristics-exec-result.c qdevice-heuristics-exec-result.h \
                           process-list.h process-list.c \
                           qdevice-net-heuristics.c qdevice-net-heuristics.h \
                           qdevice-heuristics-result-notifier.c qdevice-heuristics-result-notifier.h

corosync_qdevice_tool_SOURCES = corosync-qdevice-tool.c unix-socket.c unix-socket.h dynar.c dynar.h \
                                dynar-str.c dynar-str.h utils.c utils.h

corosync_qdevice_CFLAGS	= $(nss_CFLAGS)
corosync_qdevice_LDADD	= $(nss_LIBS) $(LIBQB_LIBS) $(top_builddir)/lib/libcmap.la \
                          $(top_builddir)/lib/libvotequorum.la

corosync-qdevice-net-certutil: corosync-qdevice-net-certutil.sh
	sed -e 's#@''DATADIR@#${datadir}#g' \
	    -e 's#@''BASHPATH@#${BASHPATH}#g' \
	    -e 's#@''COROSYSCONFDIR@#${COROSYSCONFDIR}#g' \
	    $< > $@

TESTS				= qnetd-cluster-list.test dynar.test dynar-simple-lex.test \
                                  dynar-getopt-lex.test process-list.test
check_PROGRAMS			= qnetd-cluster-list.test dynar.test dynar-simple-lex.test \
                                  dynar-getopt-lex.test process-list.test

qnetd_cluster_list_test_SOURCES	= qnetd-cluster-list.c test-qnetd-cluster-list.c \
                                  qnetd-cluster.c qnetd-cluster.h \
                                  qnetd-client-list.c qnetd-client.c dynar.c node-list.c \
                                  send-buffer-list.c
qnetd_cluster_list_test_CFLAGS  = $(nss_CFLAGS)
qnetd_cluster_list_test_LDADD	= $(nss_LIBS)

dynar_test_SOURCES		= test-dynar.c dynar.c dynar-str.c
dynar_simple_lex_test_SOURCES	= test-dynar-simple-lex.c dynar.c dynar-str.c dynar-simple-lex.c
dynar_getopt_lex_test_SOURCES	= test-dynar-getopt-lex.c dynar.c dynar-str.c dynar-getopt-lex.c
process_list_test_SOURCES	= test-process-list.c dynar.c dynar-str.c dynar-simple-lex.c \
                                  process-list.c

endif
