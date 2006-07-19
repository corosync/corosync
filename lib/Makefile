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
include ../Makefile.inc

CFLAGS += -I../include
LDFLAGS += -L./

all:libSaClm.a libSaClm.so.2.0.0 \
	libSaAmf.a libSaAmf.so.2.0.0 \
	libSaCkpt.a libSaCkpt.so.2.0.0 \
	libSaEvt.a libSaEvt.so.2.0.0 \
	libSaLck.a libSaLck.so.2.0.0 \
	libSaMsg.a libSaMsg.so.2.0.0 \
	libcpg.a libcpg.so.2.0.0 \
	libais.a libais.so.2.0.0 \
	libevs.a libevs.so.2.0.0 \
	libcfg.a libcfg.so.2.0.0

LIBAIS_SRC = util.c amf.c clm.c ckpt.c evt.c

LIBEVS_SRC = util.c evs.c

libSaClm.a: util.o clm.o
	$(AR) -rc libSaClm.a util.o clm.o

ifeq (${OPENAIS_COMPAT}, DARWIN)

libSaClm.so.2.0.0: util.o clm.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o clm.o -o $@

libSaAmf.so.2.0.0: util.o amf.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o amf.o -o $@

libSaCkpt.so.2.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o ckpt.o -o $@

libSaEvt.so.2.0.0: util.o evt.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o evt.o -o $@

libSaLck.so.2.0.0: util.o lck.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o lck.o -o $@

libSaMsg.so.2.0.0: util.o msg.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o msg.o -o $@

libais.so.2.0.0: util.o amf.o clm.o ckpt.o evt.o lck.o msg.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o amf.o clm.o ckpt.o evt.o -o $@

libevs.so.2.0.0: util.o evs.o
	$(CC) $(LDFLAGS) -bundle -bind_at_load util.o evs.o -o $@

libcpg.so.2.0.0: util.o cpg.o
	$(CC) -bundle -bind_at_load util.o cpg.o -o $@

libcfg.so.2.0.0: util.o cfg.o
	$(CC) -bundle -bind_at_load util.o cfg.o -o $@

else

libSaClm.so.2.0.0: util.o clm.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaClm.so.2,-version-script=libSaClm.versions util.o clm.o -o $@

libSaAmf.so.2.0.0: util.o amf.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaAmf.so.2,-version-script=libSaAmf.versions util.o amf.o -o $@

libSaCkpt.so.2.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaCkpt.so.2,-version-script=libSaCkpt.versions util.o ckpt.o -o $@

libSaEvt.so.2.0.0: util.o evt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaEvt.so.2,-version-script=libSaEvt.versions util.o evt.o -o $@

libSaLck.so.2.0.0: util.o lck.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaLck.so.2,-version-script=libSaLck.versions util.o lck.o -o $@

libSaMsg.so.2.0.0: util.o msg.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaMsg.so.2,-version-script=libSaMsg.versions util.o msg.o -o $@

libais.so.2.0.0: util.o amf.o clm.o ckpt.o evt.o lck.o msg.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libais.so.2,-version-script=libSaAis.versions util.o amf.o clm.o ckpt.o evt.o -o $@

libevs.so.2.0.0: util.o evs.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libevs.so.2,-version-script=libevs.versions util.o evs.o -o $@

libcpg.so.2.0.0: util.o cpg.o
	$(CC) -shared -Wl,-soname,libcpg.so.2,-version-script=libcpg.versions util.o cpg.o -o $@

libcfg.so.2.0.0: util.o cfg.o
	$(CC) -shared -Wl,-soname,libcfg.so.2,-version-script=libcfg.versions util.o cfg.o -o $@

endif

libSaAmf.a: util.o amf.o
	$(AR) -rc libSaAmf.a util.o amf.o

libSaCkpt.a: util.o ckpt.o
	$(AR) -rc libSaCkpt.a util.o ckpt.o

libSaEvt.a: util.o evt.o
	$(AR) -rc libSaEvt.a util.o evt.o

libSaLck.a: util.o lck.o
	$(AR) -rc libSaLck.a util.o lck.o

libSaMsg.a: util.o msg.o
	$(AR) -rc libSaMsg.a util.o msg.o

libais.a: util.o amf.o clm.o ckpt.o evt.o msg.o
	$(AR) -rc libais.a util.o amf.o clm.o ckpt.o evt.o msg.o

libevs.a: util.o evs.o
	$(AR) -rc libevs.a util.o evs.o

libcpg.a: util.o cpg.o
	$(AR) -rc libcpg.a util.o cpg.o

libcfg.a: util.o cfg.o
	$(AR) -rc libcfg.a util.o cfg.o

clean:
	rm -f *.o libais.so* libais.a libSaClm.so* libSaClm.a* libSaAmf.so* libSaAmf.a \
		libSaCkpt.so* libSaCkpt.a* libSaEvt.so* libSaEvt.a libSaLck.so* libSaLck.a \
		libSaMsg.so* libSaMsg.a libcfg.so* libOpenaisCfg.a \
		libevs.so* libevs.a libcpg.so* libcpg.a *.da *.bb *.bbg
 
# -fPIC rules required for all libraries
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
# DO NOT DELETE

util.o: ../include/saAis.h ../include/ipc_gen.h ../exec/totemip.h util.h
amf.o: ../include/saAis.h ../include/saAmf.h ../include/saAis.h
amf.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_amf.h
amf.o: ../include/ipc_gen.h ../include/saAmf.h util.h
clm.o: ../include/saAis.h ../include/saClm.h ../include/saAis.h
clm.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_clm.h
clm.o: ../include/saClm.h ../include/ipc_gen.h util.h
ckpt.o: ../include/saAis.h ../include/list.h ../include/saCkpt.h
ckpt.o: ../include/ipc_gen.h ../exec/totemip.h ../include/ipc_ckpt.h
ckpt.o: ../include/saAis.h ../include/saCkpt.h ../include/ipc_gen.h util.h
evt.o: ../include/ipc_evt.h ../include/saAis.h ../include/saEvt.h
evt.o: ../include/saClm.h ../include/ipc_gen.h util.h ../include/ipc_gen.h
evt.o: ../exec/totemip.h ../exec/totem.h ../exec/totemip.h ../include/list.h
cpg.o: ../include/saAis.h ../include/ipc_gen.h ../exec/totemip.h
cpg.o: ../include/ipc_cpg.h ../include/saAis.h ../include/saClm.h
cpg.o: ../include/ipc_gen.h util.h
