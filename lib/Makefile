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
	libcoroutil.a libcoroutil.so.2.0.0

libcoroutil.a: util.o
	$(AR) -rc libcoroutil.a util.o

ifeq (${OPENAIS_COMPAT}, DARWIN)

DARWIN_OPTS=-dynamiclib -bind_at_load -current_version 2.0.0 -compatibility_version 2.0.0
libcoroutil.so.2.0.0: util.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o -o $@

libevs.so.2.0.0: util.o evs.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o evs.o -o $@

libcpg.so.2.0.0: util.o cpg.o
	$(CC) $(DARWIN_OPTS) util.o cpg.o -o $@

libconfdb.so.2.0.0: util.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o -o $@

libcfg.so.2.0.0: util.o cfg.o
	$(CC) $(DARWIN_OPTS) util.o cfg.o -o $@

libcpg.so.2.0.0: util.o cpg.o
	$(CC) $(DARWIN_OPTS) util.o cpg.o -o $@

else

libcoroutil.so.2.0.0: util.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libcoroutil.so.2,-version-script=$(srcdir)$(subdir)libcoroutil.versions util.o -o $@

libevs.so.2.0.0: util.o evs.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libevs.so.2,-version-script=$(srcdir)$(subdir)libevs.versions util.o evs.o -o $@

libcpg.so.2.0.0: util.o cpg.o
	$(CC) -shared -Wl,-soname,libcpg.so.2,-version-script=$(srcdir)$(subdir)libcpg.versions util.o cpg.o -o $@

libconfdb.so.2.0.0: util.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libconfdb.so.2,-version-script=$(srcdir)$(subdir)libconfdb.versions util.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o -o $@

libcfg.so.2.0.0: util.o cfg.o
	$(CC) -shared -Wl,-soname,libcfg.so.2,-version-script=$(srcdir)$(subdir)libcfg.versions util.o cfg.o -o $@

endif

libevs.a: util.o evs.o
	$(AR) -rc libevs.a util.o evs.o

libcpg.a: util.o cpg.o
	$(AR) -rc libcpg.a util.o cpg.o

libconfdb.a: util.o confdb.o sa-confdb.o
	$(AR) -rc libconfdb.a util.o confdb.o sa-confdb.o ../lcr/lcr_ifact.o

libcfg.a: util.o cfg.o
	$(AR) -rc libcfg.a util.o cfg.o

clean:
	rm -f *.o libcfg.so* libcoroutil.so* libcoroutil.a \
		libevs.so* libevs.a libcpg.so* libcpg.a libcfg.a libconfdb.so* \
		libconfdb.a libconfdb.a \ *.da *.bb *.bbg
 
# -fPIC rules required for all libraries
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
