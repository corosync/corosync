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

MAINTAINERCLEANFILES    = Makefile.in corosync/config.h.in

CS_H			= hdb.h cs_config.h cpg.h cfg.h evs.h mar_gen.h swab.h 	\
			coroipcc.h coroipcs.h coroipc_types.h corodefs.h \
			confdb.h list.h corotypes.h quorum.h votequorum.h sam.h

CS_INTERNAL_H		= ipc_cfg.h ipc_confdb.h ipc_cpg.h ipc_evs.h ipc_pload.h ipc_quorum.h 	\
			jhash.h pload.h cs_queue.h quorum.h sq.h ipc_votequorum.h coroipc_ipc.h

LCR_H			= lcr_ckpt.h lcr_comp.h	lcr_ifact.h

ENGINE_H		= config.h coroapi.h logsys.h objdb.h quorum.h

TOTEM_H			= coropoll.h totem.h totemip.h totempg.h

EXTRA_DIST 		= $(noinst_HEADERS)

noinst_HEADERS          = $(CS_INTERNAL_H:%=corosync/%)

nobase_include_HEADERS	= $(CS_H:%=corosync/%) $(LCR_H:%=corosync/lcr/%) 			\
			$(ENGINE_H:%=corosync/engine/%) $(TOTEM_H:%=corosync/totem/%)
