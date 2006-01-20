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
# Production mode flags
#CFLAGS = -O3 -Wall
#LDFLAGS = 

# Debug mode flags
CFLAGS = -g -DDEBUG -Wall
LDFLAGS = -g -L./

# Profile mode flags
#CFLAGS = -O3 -pg -DDEBUG
#LDFLAGS = -pg

# Code Coverage with lcov flgs
#CFLAGS = -ftest-coverage -fprofile-arcs
#LDFLAGS = -g


CFLAGS += -fPIC

all:liblcr.a test uic libtest_a.lcrso libtest_b.lcrso

liblcr.a: lcr_ifact.o
	$(AR) -rc liblcr.a lcr_ifact.o

libtest_a.lcrso: libtest_a.o
	$(CC) -shared -Wl,-soname,libtest_b.lcrso libtest_a.o -o $@

libtest_b.lcrso: libtest_b.o
	$(CC) -shared -Wl,-soname,libtest_b.lcrso libtest_b.o -o $@
	
test: test.o uis.o lcr_ifact.o
	$(CC) $(LDFLAGS) test.o lcr_ifact.o uis.o -lpthread -ldl -o test
	
uic: uic.o
	$(CC) $(LDFLAGS) uic.o -o uic

clean:
	rm -f test libtest.so* *.o uic liblcr.so* liblcr.a *.lcrso *.da *.ba *.bb *.bbg
