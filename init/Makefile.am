# Copyright (c) 2004 MontaVista Software, Inc.
# Copyright (c) 2009 Red Hat, Inc.
#
# Authors: Steven Dake (sdake@redhat.com)
#          Fabio M. Di Nitto (fdinitto@redhat.com)
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

MAINTAINERCLEANFILES	= Makefile.in

EXTRA_DIST		= corosync.in corosync-notifyd.in corosync.service.in \
                          corosync-notifyd.service.in corosync.conf.in corosync-notifyd.conf.in \
                          corosync.sysconfig.example \
                          corosync-qdevice.sysconfig.example corosync-qdevice.in \
                          corosync-qdevice.service.in \
                          corosync-qnetd.sysconfig.example corosync-qnetd.in \
                          corosync-qnetd.service.in

if INSTALL_SYSTEMD
systemdconfdir	   = $(SYSTEMDDIR)
systemdconf_DATA   = corosync.service corosync-notifyd.service
initscriptdir	   = $(INITWRAPPERSDIR)
else
initscriptdir	   = $(INITDDIR)
endif
initscript_SCRIPTS  = corosync corosync-notifyd

if BUILD_QDEVICES
initscript_SCRIPTS  += corosync-qdevice
if INSTALL_SYSTEMD
systemdconf_DATA   += corosync-qdevice.service
endif
endif

if BUILD_QNETD
initscript_SCRIPTS  += corosync-qnetd
if INSTALL_SYSTEMD
systemdconf_DATA   += corosync-qnetd.service
endif
endif

if INSTALL_UPSTART
upstartconfdir     = $(UPSTARTDIR)
upstartconf_DATA   = corosync.conf corosync-notifyd.conf
endif

%: %.in Makefile
	rm -f $@-t $@
	cat $< | sed \
		-e 's#@''SBINDIR@#$(sbindir)#g' \
		-e 's#@''BINDIR@#$(bindir)#g' \
		-e 's#@''SYSCONFDIR@#$(sysconfdir)#g' \
		-e 's#@''INITDDIR@#$(INITDDIR)#g' \
		-e 's#@''INITWRAPPERSDIR@#$(INITWRAPPERSDIR)#g' \
		-e 's#@''LOCALSTATEDIR@#$(localstatedir)#g' \
		-e 's#@''BASHPATH@#${BASHPATH}#g' \
	    > $@-t
	mv $@-t $@

all-local: $(initscript_SCRIPTS) $(systemdconf_DATA) $(upstartconf_DATA)

clean-local:
	rm -rf $(initscript_SCRIPTS) $(systemdconf_DATA) $(upstartconf_DATA)
