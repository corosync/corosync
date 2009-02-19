# Copyright (c) 2002-2005 MontaVista Software, Inc.
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

# Include configuration
#
srcdir ?= $(CURDIR)/../
subdir ?= lib/

include $(srcdir)Makefile.inc

override LDFLAGS += -L./

all: libcpg.a libcpg.so.2.0.0 \
	libconfdb.a libconfdb.so.2.0.0 \
	libevs.a libevs.so.2.0.0 \
	libcfg.a libcfg.so.2.0.0 \
	libquorum.a libquorum.so.2.0.0 \
	libpload.a libpload.so.2.0.0 \
	libcoroipc.a libcoroipc.so.2.0.0 \
	libvotequorum.a libvotequorum.so.2.0.0

libcoroipc.a: coroipc.o
	$(AR) -rc libcoroipc.a coroipc.o

ifeq (${COROSYNC_COMPAT}, DARWIN)

DARWIN_OPTS=-dynamiclib -bind_at_load -current_version 2.0.0 -compatibility_version 2.0.0
libcoroipc.so.2.0.0: coroipc.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipc.o -o $@

libevs.so.2.0.0: coroipc.o evs.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipc.o evs.o -o $@

libcpg.so.2.0.0: coroipc.o cpg.o
	$(CC) $(DARWIN_OPTS) coroipc.o cpg.o -o $@

libquorum.so.2.0.0: coroipc.o quorum.o
	$(CC) $(DARWIN_OPTS) coroipc.o quorum.o -o $@

libvotequorum.so.2.0.0: coroipc.o votequorum.o
	$(CC) $(DARWIN_OPTS) coroipc.o votequorum.o -o $@

libconfdb.so.2.0.0: coroipc.o confdb.o sa-confdb.o 
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) coroipc.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o -o $@

libcfg.so.2.0.0: coroipc.o cfg.o
	$(CC) $(DARWIN_OPTS) coroipc.o cfg.o -o $@

libpload.so.2.0.0: coroipc.o pload.o
	$(CC) $(DARWIN_OPTS) coroipc.o pload.o -o $@

else

libcoroipc.so.2.0.0: coroipc.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libcoroipc.so.2,-version-script=$(srcdir)$(subdir)libcoroipc.versions coroipc.o -o $@

libevs.so.2.0.0: coroipc.o evs.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libevs.so.2,-version-script=$(srcdir)$(subdir)libevs.versions coroipc.o evs.o -o $@

libcpg.so.2.0.0: coroipc.o cpg.o
	$(CC) -shared -Wl,-soname,libcpg.so.2,-version-script=$(srcdir)$(subdir)libcpg.versions coroipc.o cpg.o -o $@

libquorum.so.2.0.0: coroipc.o quorum.o
	$(CC) -shared -Wl,-soname,libquorum.so.2,-version-script=$(srcdir)$(subdir)libquorum.versions coroipc.o quorum.o -o $@

libvotequorum.so.2.0.0: coroipc.o votequorum.o
	$(CC) -shared -Wl,-soname,libvotequorum.so.2,-version-script=$(srcdir)$(subdir)libvotequorum.versions coroipc.o votequorum.o -o $@

libconfdb.so.2.0.0: coroipc.o confdb.o sa-confdb.o 
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libconfdb.so.2,-version-script=$(srcdir)$(subdir)libconfdb.versions coroipc.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o -o $@

libcfg.so.2.0.0: coroipc.o cfg.o
	$(CC) -shared -Wl,-soname,libcfg.so.2,-version-script=$(srcdir)$(subdir)libcfg.versions coroipc.o cfg.o -o $@

libpload.so.2.0.0: coroipc.o pload.o
	$(CC) -shared -Wl,-soname,libpload.so.2,-version-script=$(srcdir)$(subdir)libpload.versions coroipc.o cfg.o -o $@

endif

libevs.a: coroipc.o evs.o
	$(AR) -rc libevs.a coroipc.o evs.o

libcpg.a: coroipc.o cpg.o
	$(AR) -rc libcpg.a coroipc.o cpg.o

libquorum.a: coroipc.o quorum.o
	$(AR) -rc libquorum.a coroipc.o quorum.o

libvotequorum.a: coroipc.o votequorum.o
	$(AR) -rc libvotequorum.a coroipc.o votequorum.o

libconfdb.a: coroipc.o confdb.o sa-confdb.o
	$(AR) -rc libconfdb.a coroipc.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o

libcfg.a: coroipc.o cfg.o
	$(AR) -rc libcfg.a coroipc.o cfg.o

libpload.a: coroipc.o pload.o
	$(AR) -rc libpload.a coroipc.o pload.o

clean:
	rm -f *.o *.a *.so* *.da *.bb *.bbg
 
lint:
	-splint $(LINT_FLAGS) $(CFLAGS) *.c

# -fPIC rules required for all libraries
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
