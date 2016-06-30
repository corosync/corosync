# Copyright (c) 2004 MontaVista Software, Inc.
# Copyright (c) 2009, 2012, 2014 Red Hat, Inc.
#
# Authors: Steven Dake (sdake@redhat.com)
#          Fabio M. Di Nitto (fdinitto@redhat.com)
#
# All rights reserved.
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

MAINTAINERCLEANFILES	= Makefile.in

xml_man			= corosync-xmlproc.8 \
			  corosync.xml.5

qnetd_man		= corosync-qnetd-tool.8 \
                          corosync-qnetd-certutil.8 \
                          corosync-qnetd.8

qdevices_man		= corosync-qdevice-tool.8 \
                          corosync-qdevice-net-certutil.8 \
                          corosync-qdevice.8

INDEX_HTML		= index.html

autogen_man		= cpg_context_get.3 \
			  cpg_context_set.3 \
			  cpg_dispatch.3 \
			  cpg_fd_get.3 \
			  cpg_finalize.3 \
			  cpg_initialize.3 \
			  cpg_join.3 \
			  cpg_leave.3 \
			  cpg_local_get.3 \
			  cpg_mcast_joined.3 \
			  cpg_model_initialize.3 \
			  cpg_zcb_mcast_joined.3 \
			  cpg_zcb_alloc.3 \
			  cpg_zcb_free.3 \
			  cpg_membership_get.3 \
			  cpg_iteration_finalize.3 \
			  cpg_iteration_initialize.3 \
			  cpg_iteration_next.3 \
			  quorum_initialize.3 \
			  quorum_finalize.3 \
			  quorum_fd_get.3 \
			  quorum_dispatch.3 \
			  quorum_context_get.3 \
			  quorum_context_set.3 \
			  quorum_getquorate.3 \
			  quorum_trackstart.3 \
			  quorum_trackstop.3 \
			  votequorum_dispatch.3 \
			  votequorum_fd_get.3 \
			  votequorum_context_get.3 \
			  votequorum_context_set.3 \
			  votequorum_finalize.3 \
			  votequorum_getinfo.3 \
			  votequorum_initialize.3 \
			  votequorum_setexpected.3 \
			  votequorum_setvotes.3 \
			  votequorum_trackstart.3 \
			  votequorum_trackstop.3 \
			  votequorum_qdevice_register.3 \
			  votequorum_qdevice_unregister.3 \
			  votequorum_qdevice_update.3 \
			  votequorum_qdevice_master_wins.3 \
			  votequorum_qdevice_poll.3 \
			  sam_data_getsize.3 \
			  sam_data_restore.3 \
			  sam_data_store.3 \
			  sam_finalize.3 \
			  sam_hc_callback_register.3 \
			  sam_hc_send.3 \
			  sam_initialize.3 \
			  sam_mark_failed.3 \
			  sam_register.3 \
			  sam_start.3 \
			  sam_stop.3 \
			  sam_warn_signal_set.3 \
			  cmap_context_get.3 \
			  cmap_dec.3 \
			  cmap_iter_init.3 \
			  cmap_get.3 \
			  cmap_inc.3 \
			  cmap_set.3 \
			  cmap_iter_next.3 \
			  cmap_delete.3 \
			  cmap_iter_finalize.3 \
			  cmap_finalize.3 \
			  cmap_dispatch.3  \
			  cmap_initialize.3 \
			  cmap_track_add.3 \
			  cmap_context_set.3 \
			  cmap_fd_get.3 \
			  cmap_track_delete.3

autogen_common		= ipc_common.sh.errors

EXTRA_DIST		= $(INDEX_HTML) \
			  $(xml_man) \
			  $(qnetd_man) \
			  $(qdevices_man) \
			  $(autogen_man:%=%.in) \
			  $(autogen_common)

man_MANS		= $(autogen_man)

dist_man_MANS 		= corosync.conf.5 \
			  votequorum.5 \
			  corosync.8 \
			  corosync-cmapctl.8 \
			  corosync-blackbox.8 \
			  corosync-keygen.8 \
			  corosync-cfgtool.8 \
			  corosync-cpgtool.8 \
			  corosync-notifyd.8 \
			  corosync-quorumtool.8 \
			  corosync_overview.8 \
			  cpg_overview.8 \
			  quorum_overview.8 \
			  votequorum_overview.8 \
			  sam_overview.8 \
			  cmap_overview.8 \
			  cmap_keys.8

if INSTALL_XMLCONF
dist_man_MANS		+= $(xml_man)
endif

if BUILD_QNETD
dist_man_MANS		+= $(qnetd_man)
endif

if BUILD_QDEVICES
dist_man_MANS		+= $(qdevices_man)
endif

HTML_DOCS 		= $(dist_man_MANS:%=%.html) $(man_MANS:%=%.html)

# developer man page generation
%.3: %.3.in $(autogen_common)
	@echo Generating $@ man page && \
	rm -f $@-t-t $@-t $@ && \
	date="$$(LC_ALL=C date "+%F")" && \
	awk "{print}(\$$1 ~ /@COMMONIPCERRORS@/){exit 0}" ${top_srcdir}/man/$@.in > $@-t-t && \
	cat ${top_srcdir}/man/$(autogen_common) >> $@-t-t && \
	awk -v p=0 "(\$$1 ~ /@COMMONIPCERRORS@/){p = 1} {if(p==1)print}" ${top_srcdir}/man/$@.in >> $@-t-t && \
	cat $@-t-t | \
		sed -e 's#@BUILDDATE@#'$$date'#g' \
		    -e 's#@COMMONIPCERRORS@##g' \
		    > $@-t && \
	rm -f $@-t-t && \
	mv $@-t $@

clean-local:
	rm -rf $(HTML_DOCS) $(autogen_man)

if BUILD_HTML_DOCS
%.html: %
	$(GROFF) -mandoc -Thtml $^ > $@

install-data-local:
	$(INSTALL) -d $(DESTDIR)/${docdir}/html
	$(INSTALL) -m 644 ${srcdir}/$(INDEX_HTML) $(HTML_DOCS) $(DESTDIR)/${docdir}/html/

uninstall-local:
	cd $(DESTDIR)/${docdir}/html && rm -f $(INDEX_HTML) $(HTML_DOCS)
	rmdir $(DESTDIR)/${docdir}/html 2> /dev/null || :

all-local: $(HTML_DOCS)
endif
