#
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

# Functions to be used at link time (target.lo)
get_libname=$(shell echo $1 | sed -e "s/\.lo//")
uc=$(shell echo $(call get_libname,$1) | tr a-z A-Z)
get_soname=$(subst .,:,$(if $($(call uc,$1)_SONAME),$($(call uc,$1)_SONAME),$(SONAME)))
get_major=$(firstword $(subst :, ,$(call get_soname,$1)))

MAINTAINERCLEANFILES    = Makefile.in

EXTRA_DIST		= libcfg.versions	 libcfg.verso		\
			  libcmap.versions	 libcmap.verso		\
			  libcpg.versions	 libcpg.verso    	\
			  libquorum.versions	 libquorum.verso 	\
			  libsam.versions	 libsam.verso    	\
			  libvotequorum.versions libvotequorum.verso

noinst_HEADERS		= util.h

# override global LIBS that pulls in lots of craft we don't need here
LIBS			= -version-number $(call get_soname,$<) \
			  @VERSCRIPT_LDFLAGS@ \
			  -lpthread \
			  $(top_builddir)/common_lib/libcorosync_common.la \
			  $(LIBQB_LIBS)

lib_LTLIBRARIES		= libcpg.la libquorum.la libcfg.la \
			  libvotequorum.la libcmap.la libsam.la

libcpg_la_SOURCES	= cpg.c
libcfg_la_SOURCES	= cfg.c
libquorum_la_SOURCES	= quorum.c
libvotequorum_la_SOURCES= votequorum.c
libcmap_la_SOURCES	= cmap.c
libsam_la_SOURCES	= sam.c
libsam_la_LIBADD	= libquorum.la libcmap.la
