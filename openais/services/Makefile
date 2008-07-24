# Copyright (c) 2002-2006 MontaVista Software, Inc.
# Copyright (c) 2006 Sun Microsystems, Inc.
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

include $(srcdir)Makefile.inc

override CFLAGS += -I$(srcdir)include
LDFLAGS += -L./

ifeq (${OPENAIS_COMPAT}, LINUX) 
	override LDFLAGS += -ldl
endif

ifeq (${OPENAIS_COMPAT}, SOLARIS) 
	override LDFLAGS += -lnsl -lsocket -lrt
endif

# AMF objects
AMF_SRC = amf.c amfutil.c amfnode.c amfcluster.c amfapp.c amfsg.c amfsu.c amfcomp.c amfsi.c
AMF_OBJS = amf.o amfutil.o amfnode.o amfcluster.o amfapp.o amfsg.o amfsu.o amfcomp.o amfsi.o

# LCR objects
LCR_SRC = openaisparser.c clm.c ckpt.c evt.c lck.c msg.c cfg.c openaisparser.c $(AMF_SRC)
LCR_OBJS = openaisparser.o clm.o ckpt.o evt.o lck.o msg.o cfg.o openaisparser.o $(AMF_OBJS)

override CFLAGS += -fPIC

all: openaisparser.lcrso service_clm.lcrso service_ckpt.lcrso \
	service_evt.lcrso service_lck.lcrso service_msg.lcrso \
	openais-instantiate

ifeq (${OPENAIS_COMPAT}, DARWIN)

service_evs.lcrso: evs.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load evs.o -o $@

service_clm.lcrso: clm.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load clm.o -o $@

service_amf.lcrso: $(AMF_OBJS)
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load $(AMF_OBJS) -o $@

service_ckpt.lcrso: ckpt.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load ckpt.o -o $@

service_evt.lcrso: evt.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load evt.o -o $@

service_lck.lcrso: lck.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load lck.o -o $@

service_msg.lcrso: msg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load msg.o -o $@

service_cfg.lcrso: cfg.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load cfg.o -o $@

openaisparser.lcrso: openaisparser.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load openaisparser.o -o $@

vsf_ykd.lcrso: vsf_ykd.o
	$(CC) $(LDFLAGS) -bundle $(LDFLAGS) -bundle_loader ./aisexec -bind_at_load vsf_ykd.o -o $@


else

service_clm.lcrso: clm.o
	$(CC) -shared -Wl,-soname,service_clm.lcrso clm.o -o $@

service_amf.lcrso: $(AMF_OBJS)
	$(CC) -shared -Wl,-soname,service_amf.lcrso $(AMF_OBJS) -o $@

service_ckpt.lcrso: ckpt.o
	$(CC) -shared -Wl,-soname,service_ckpt.lcrso ckpt.o -o $@

service_evt.lcrso: evt.o
	$(CC) -shared -Wl,-soname,service_evt.lcrso evt.o -o $@

service_lck.lcrso: lck.o
	$(CC) -shared -Wl,-soname,service_lck.lcrso lck.o -o $@

service_msg.lcrso: msg.o
	$(CC) -shared -Wl,-soname,service_msg.lcrso msg.o -o $@

service_cfg.lcrso: cfg.o
	$(CC) -shared -Wl,-soname,service_cfg.lcrso cfg.o -o $@

openaisparser.lcrso: openaisparser.o
	$(CC) -shared -Wl,-soname,openaisparser.lcrso openaisparser.o -o $@

vsf_ykd.lcrso: vsf_ykd.o
	$(CC) -shared -Wl,-soname,vsf_ykd.lcrso vsf_ykd.o -o $@

endif

keygen: keygen.o
	$(CC) $(LDFLAGS) keygen.o -o keygen

openais-instantiate: openais-instantiate.o
	$(CC) $(LDFLAGS) openais-instantiate.o -o openais-instantiate

clean:
	rm -f aisexec $(OBJS) *.o *.lcrso libtotem_pg.so* libtotem_pg.a gmon.out 
	rm -f keygen keygen.o openais-instantiate *.da *.bb *.bbg liblogsys.so* liblogsys.a

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(EXEC_SRC) $(TOTEM_SRC) $(LOGSYS_SRC) $(LCR_SRC) > /dev/null 2>&1

# - fPIC rules required for service handler shared objects
../lcr/lcr_ifact.o: ../lcr/lcr_ifact.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DPREFIX='"$(PREFIX)"' -DLCRSODIR='"$(LCRSODIR)"' -I../lcr -c -o $@ ../lcr/lcr_ifact.c

clm.o: clm.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

amf.o: amf.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

ckpt.o: ckpt.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

evt.o: evt.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

lck.o: lck.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

msg.o: msg.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

openaisparser.o: openaisparser.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
