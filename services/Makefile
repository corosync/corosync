# Copyright (c) 2006-2008 Red Hat, Inc.
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
subdir ?= services/

include $(srcdir)Makefile.inc

override LDFLAGS += -L./

ifeq (${BUILD_DYNAMIC}, 1) 
	override LDFLAGS += ${DYFLAGS}
endif

ifeq (${COROSYNC_COMPAT}, LINUX) 
	override LDFLAGS += -ldl
endif

ifeq (${COROSYNC_COMPAT}, SOLARIS) 
	override LDFLAGS += -lnsl -lsocket -lrt
endif

# LCR objects
LCR_SRC = evs.c cfg.c cpg.c confdb.c pload.c
LCR_OBJS = evs.o cfg.o cpg.o confdb.o $(AMF_OBJS) pload.o

override CFLAGS += -fPIC

all: service_evs.lcrso service_cfg.lcrso service_cpg.lcrso service_confdb.lcrso service_pload.lcrso testquorum.lcrso service_votequorum.lcrso

ifeq (${COROSYNC_COMPAT}, DARWIN)

service_evs.lcrso: evs.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load evs.o -o $@

service_cfg.lcrso: cfg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load cfg.o -o $@

service_confdb.lcrso: confdb.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load confdb.o -o $@

service_cpg.lcrso: cpg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load cpg.o -o $@

service_pload.lcrso: pload.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load pload.o -o $@

service_votequorum.lcrso: votequorum.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load votequorum.o -o $@

testquorum.lcrso: testquorum.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ../exec/corosync -bind_at_load testquorum.o -o $@

else

service_evs.lcrso: evs.o
	$(CC) -shared -Wl,-soname,service_evs.lcrso evs.o -o $@

service_cfg.lcrso: cfg.o
	$(CC) -shared -Wl,-soname,service_cfg.lcrso cfg.o -o $@

service_confdb.lcrso: confdb.o
	$(CC) -shared -Wl,-soname,service_confdb.lcrso confdb.o -o $@

service_cpg.lcrso: cpg.o
	$(CC) -shared -Wl,-soname,service_cpg.lcrso cpg.o -o $@

service_pload.lcrso: pload.o
	$(CC) -shared -Wl,-soname,service_pload.lcrso pload.o -o $@

service_votequorum.lcrso: votequorum.o
	$(CC) -shared -Wl,-soname,service_votequorum.lcrso votequorum.o -o $@

testquorum.lcrso: testquorum.o
	$(CC) -shared -Wl,-soname,testquorum.lcrso testquorum.o -o $@

endif

clean:
	rm -f *.o *.lcrso 

lint:
	-splint $(LINT_FLAGS) $(CFLAGS) *.c

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(EXEC_SRC) $(TOTEM_SRC) $(LOGSYS_SRC) $(LCR_SRC) > /dev/null 2>&1

evs.o: evs.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

cfg.o: cfg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

confdb.o: confdb.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

cpg.o: cpg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

testquorum.o: testquorum.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
