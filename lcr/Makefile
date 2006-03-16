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
LDFLAGS += -L./ ${DYFLAGS}

ifeq (${OPENAIS_COMPAT}, LINUX) 
	LDFLAGS += -ldl
endif

all:liblcr.a test test_static uic libtest_a.lcrso libtest_b.lcrso

liblcr.a: lcr_ifact.o
	$(AR) -rc liblcr.a lcr_ifact.o

ifeq (${OPENAIS_COMPAT}, DARWIN)

libtest_a.lcrso: libtest_a.o
	$(CC) $(CFLAGS) -bundle -bundle_loader ./test libtest_a.o -o $@

libtest_b.lcrso: libtest_b.o
	$(CC) $(CFLAGS) -bundle -bundle_loader ./test libtest_b.o -o $@

else

libtest_a.lcrso: libtest_a.o
	$(CC) $(CFLAGS) -shared -Wl,-soname,libtest_b.lcrso libtest_a.o -o $@

libtest_b.lcrso: libtest_b.o
	$(CC) $(CFLAGS) -shared -Wl,-soname,libtest_b.lcrso libtest_b.o -o $@

endif
	
test: test.o uis.o lcr_ifact.o
	$(CC) $(LDFLAGS) test.o lcr_ifact.o uis.o -lpthread -o test
	
test_static: test.o libtest_a.o libtest_b.o uis.o lcr_ifact.o
	$(CC) $(LDFLAGS) test.o libtest_a.o libtest_b.o lcr_ifact.o -o test_static

uic: uic.o
	$(CC) $(LDFLAGS) uic.o -o uic

libtest_a.o: libtest_a.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

libtest_b.o: libtest_b.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $(*F).c

clean:
	rm -f test libtest.so* *.o uic liblcr.so* liblcr.a *.lcrso *.da *.ba *.bb *.bbg \
	test_static

