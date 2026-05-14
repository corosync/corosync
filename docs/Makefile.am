# Copyright (c) 2026 Red Hat, Inc.
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
EXTRA_DIST              = man.dox.in
noinst_HEADERS          = mainpage.h

# To be added when this is the main docs directory.
#dist_man_MANS = \
#	man8/corosync.8 \
#	man8/corosync-blackbox.8 \
#	man8/corosync-cfgtool.8 \
#	man8/corosync-cmapctl.8 \
#	man8/corosync-cpgtool.8 \
#	man8/corosync-keygen.8 \
#	man8/corosync-notifyd.8 \
#	man8/corosync-quorumtool.8 \
#	man8/corosync-vqsim.8 \
#	man8/corosync-xmlproc.8

if HAVE_DOXYGEN
if BUILD_MAN

doxygen2man_flags = \
	-c -m -P -q \
	-i corosync/ \
	-o man3/ \
	-H "corosync Programmer's Manual" \
	$$($(UTC_DATE_AT)$${SOURCE_DATE_EPOCH:-$$(date +%s)} +"-D %F -Y %Y") \
	-p COROSYNC \
	-O ../include/corosync

man3_MANS = \
	man3/cmap_context_get.3 \
	man3/cmap_context_set.3 \
	man3/cmap_dec.3 \
	man3/cmap_delete.3 \
	man3/cmap_dispatch.3 \
	man3/cmap_fd_get.3 \
	man3/cmap_finalize.3 \
	man3/cmap_get.3 \
	man3/cmap_inc.3 \
	man3/cmap_initialize.3 \
	man3/cmap_initialize_map.3 \
	man3/cmap_iter_finalize.3 \
	man3/cmap_iter_init.3 \
	man3/cmap_iter_next.3 \
	man3/cmap_overview.3 \
	man3/cmap_set.3 \
	man3/cmap_track_add.3 \
	man3/cmap_track_delete.3 \
	man3/cpg_context_get.3 \
	man3/cpg_context_set.3 \
	man3/cpg_dispatch.3 \
	man3/cpg_fd_get.3 \
	man3/cpg_finalize.3 \
	man3/cpg_initialize.3 \
	man3/cpg_iteration_finalize.3 \
	man3/cpg_iteration_initialize.3 \
	man3/cpg_iteration_next.3 \
	man3/cpg_join.3 \
	man3/cpg_leave.3 \
	man3/cpg_local_get.3 \
	man3/cpg_mcast_joined.3 \
	man3/cpg_membership_get.3 \
	man3/cpg_model_initialize.3 \
	man3/cpg_overview.3 \
	man3/cpg_zcb_alloc.3 \
	man3/cpg_zcb_free.3 \
	man3/cpg_zcb_mcast_joined.3 \
	man3/quorum_context_get.3 \
	man3/quorum_context_set.3 \
	man3/quorum_dispatch.3 \
	man3/quorum_fd_get.3 \
	man3/quorum_finalize.3 \
	man3/quorum_getquorate.3 \
	man3/quorum_initialize.3 \
	man3/quorum_model_initialize.3 \
	man3/quorum_overview.3 \
	man3/quorum_trackstart.3 \
	man3/quorum_trackstop.3 \
	man3/sam_data_getsize.3 \
	man3/sam_data_restore.3 \
	man3/sam_data_store.3 \
	man3/sam_finalize.3 \
	man3/sam_hc_callback_register.3 \
	man3/sam_hc_send.3 \
	man3/sam_initialize.3 \
	man3/sam_mark_failed.3 \
	man3/sam_overview.3 \
	man3/sam_register.3 \
	man3/sam_start.3 \
	man3/sam_stop.3 \
	man3/sam_warn_signal_set.3 \
	man3/votequorum_context_get.3 \
	man3/votequorum_context_set.3 \
	man3/votequorum_dispatch.3 \
	man3/votequorum_fd_get.3 \
	man3/votequorum_finalize.3 \
	man3/votequorum_getinfo.3 \
	man3/votequorum_initialize.3 \
	man3/votequorum_overview.3 \
	man3/votequorum_qdevice_master_wins.3 \
	man3/votequorum_qdevice_poll.3 \
	man3/votequorum_qdevice_register.3 \
	man3/votequorum_qdevice_unregister.3 \
	man3/votequorum_qdevice_update.3 \
	man3/votequorum_setexpected.3 \
	man3/votequorum_setvotes.3 \
	man3/votequorum_trackstart.3 \
	man3/votequorum_trackstop.3

$(man3_MANS): txt-man

txt-man: man.dox
	doxygen man.dox

xml-man: txt-man
	mkdir -p man3
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) cmap_8h.xml
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) corodefs_8h.xml
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) corotypes_8h.xml
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) quorum_8h.xml
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) sam_8h.xml
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) votequorum_8h.xml
	"$(DOXYGEN2MAN)" $(doxygen2man_flags) cpg_8h.xml

all: $(man3_MANS) xml-man

endif
endif

clean-local:
	rm -rf man3 xml
