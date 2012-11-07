# Copyright (c) 2004 MontaVista Software, Inc.
# Copyright (c) 2009 Red Hat, Inc.
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

EXTRA_DIST		= index.html

dist_man_MANS = \
	corosync.conf.5 \
	confdb_overview.8 \
	corosync.8 \
	corosync-objctl.8 \
	corosync-blackbox.8 \
	corosync-keygen.8 \
	corosync-cfgtool.8 \
	corosync-cpgtool.8 \
	corosync-fplay.8 \
	corosync-pload.8 \
	corosync-notifyd.8 \
	corosync-quorumtool.8 \
	corosync_overview.8 \
	cpg_overview.8 \
	evs_overview.8 \
	logsys_overview.8 \
	votequorum_overview.8 \
	coroipc_overview.8 \
	confdb_dispatch.3 \
	confdb_fd_get.3 \
	confdb_finalize.3 \
	confdb_initialize.3 \
	confdb_key_create.3 \
	confdb_key_delete.3 \
	confdb_key_get.3 \
	confdb_key_iter.3 \
	confdb_key_iter_start.3 \
	confdb_key_replace.3 \
	confdb_object_create.3 \
	confdb_object_destroy.3 \
	confdb_object_find.3 \
	confdb_object_find_start.3 \
	confdb_object_iter.3 \
	confdb_object_iter_start.3 \
	confdb_object_parent_get.3 \
	confdb_context_get.3 \
	confdb_context_set.3 \
	confdb_keys.8 \
	cpg_context_get.3 \
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
	evs_dispatch.3 \
	evs_fd_get.3 \
	evs_finalize.3 \
	evs_initialize.3 \
	evs_join.3 \
	evs_leave.3 \
	evs_mcast_groups.3 \
	evs_mcast_joined.3 \
	evs_membership_get.3 \
	evs_context_get.3 \
	evs_context_set.3 \
	votequorum_dispatch.3 \
	votequorum_fd_get.3 \
	votequorum_context_get.3 \
	votequorum_context_set.3 \
	votequorum_finalize.3 \
	votequorum_getinfo.3 \
	votequorum_initialize.3 \
	votequorum_leaving.3 \
	votequorum_qdisk_getinfo.3 \
	votequorum_qdisk_poll.3 \
	votequorum_qdisk_register.3 \
	votequorum_qdisk_unregister.3 \
	votequorum_setexpected.3 \
	votequorum_setvotes.3 \
	sam_finalize.3 \
	sam_hc_callback_register.3 \
	sam_hc_send.3 \
	sam_initialize.3 \
	sam_overview.8 \
	sam_register.3 \
	sam_start.3 \
	sam_stop.3 \
	sam_warn_signal_set.3
	
if BUILD_HTML_DOCS

HTML_DOCS = $(dist_man_MANS:%=%.html)

INDEX_HTML = index.html

%.html: %
	$(GROFF) -mandoc -Thtml $^ > $@

install-data-local:
	$(INSTALL) -d $(DESTDIR)/${docdir}/html
	$(INSTALL) -m644 ${srcdir}/$(INDEX_HTML) $(HTML_DOCS) $(DESTDIR)/${docdir}/html/

uninstall-local:
	cd $(DESTDIR)/${docdir}/html && rm -f $(INDEX_HTML) $(HTML_DOCS)
	rmdir $(DESTDIR)/${docdir}/html 2> /dev/null || :

all-local: $(HTML_DOCS)

clean-local:
	rm -rf $(HTML_DOCS)

endif
