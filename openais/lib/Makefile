# Copyright (c) 2002-2005 MontaVista Software, Inc.
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
override LDFLAGS += -L./

all: libSaClm.a libSaClm.so.2.0.0 \
	libSaCkpt.a libSaCkpt.so.2.0.0 \
	libSaEvt.a libSaEvt.so.2.0.0 \
	libSaMsg.a libSaMsg.so.2.0.0  \
	libSaLck.a libSaLck.so.2.0.0 

libcoroutil.a: util.o
	$(AR) -rc libcoroutil.a util.o

ifeq (${OPENAIS_COMPAT}, DARWIN)

DARWIN_OPTS=-dynamiclib -bind_at_load -current_version 2.0.0 -compatibility_version 2.0.0

libSaClm.so.2.0.0: util.o clm.o
	$(CC) $(DARWIN_OPTS) util.o clm.o -o $@

libSaCkpt.so.2.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o ckpt.o -o $@

libSaEvt.so.2.0.0: util.o evt.o sa-evt.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o evt.o

libSaMsg.so.2.0.0: util.o clm.o
	$(CC) $(DARWIN_OPTS) util.o msg.o -o $@

libSaLck.so.2.0.0: util.o clm.o
	$(CC) $(DARWIN_OPTS) util.o lck.o -o $@

else

libSaClm.so.2.0.0: util.o clm.o
	$(CC) -shared -Wl,-soname,libclm.so.2,-version-script=libSaClm.versions util.o clm.o -o $@

libSaCkpt.so.2.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaCkpt.so.2,-version-script=libSaCkpt.versions util.o ckpt.o -o $@

libSaEvt.so.2.0.0: util.o evt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaEvt.so.2,-version-script=libSaEvt.versions util.o evt.o

libSaMsg.so.2.0.0: util.o msg.o
	$(CC) -shared -Wl,-soname,libmsg.so.2,-version-script=libSaMsg.versions util.o msg.o -o $@

libSaLck.so.2.0.0: util.o lck.o
	$(CC) -shared -Wl,-soname,libSaLck.so.2,-version-script=libSaLck.versions util.o lck.o -o $@

endif

libSaCkpt.a: util.o ckpt.o
	$(AR) -rc libSaCkpt.a util.o ckpt.o

libSaClm.a: util.o clm.o
	$(AR) -rc libSaClm.a util.o clm.o

libSaEvt.a: util.o evt.o
	$(AR) -rc libSaEvt.a util.o evt.o

libSaMsg.a: util.o msg.o
	$(AR) -rc libSaMsg.a util.o evt.o

libSaLck.a: util.o msg.o
	$(AR) -rc libSaLck.a util.o msg.o

clean:
	rm -f *.o libSa*.so.* libSa*.a *.da *.bb *.bbg
 
# -fPIC rules required for all libraries
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
