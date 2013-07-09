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

SPEC			= $(PACKAGE_NAME).spec

TARFILE			= $(PACKAGE_NAME)-$(VERSION).tar.gz

EXTRA_DIST		= autogen.sh conf/corosync.conf.example $(SPEC).in \
			  build-aux/git-version-gen \
			  build-aux/gitlog-to-changelog \
			  build-aux/release.mk \
			  conf/corosync.conf.example.udpu \
			  .version

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf

dist_doc_DATA		= LICENSE INSTALL README.devmap \
			  README.recovery SECURITY TODO AUTHORS

corosysconfdir		= ${COROSYSCONFDIR}

corosysconf_DATA	= conf/corosync.conf.example \
			  conf/corosync.conf.example.udpu

SUBDIRS			= include lcr lib exec services tools test pkgconfig \
			  man init conf

coverity:
	rm -rf cov
	make clean
	cov-build --dir=cov make
	cov-analyze --dir cov \
	    --concurrency \
	    -co BAD_FREE:allow_first_field:true \
	    --security \
	    --wait-for-license
	cov-format-errors --dir cov

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/${COROSYSCONFDIR}/service.d
	$(INSTALL) -d $(DESTDIR)/${COROSYSCONFDIR}/uidgid.d
	$(INSTALL) -d $(DESTDIR)/${localstatedir}/lib/corosync
	$(INSTALL) -d $(DESTDIR)/${localstatedir}/log/cluster
	$(INSTALL) -d $(DESTDIR)/${SOCKETDIR}

uninstall-local:
	rmdir $(DESTDIR)/${COROSYSCONFDIR}/service.d || :;
	rmdir $(DESTDIR)/${COROSYSCONFDIR}/uidgid.d || :;
	rmdir $(DESTDIR)/${localstatedir}/lib/corosync || :;
	rmdir $(DESTDIR)/${localstatedir}/log/cluster || :;
	rmdir $(DESTDIR)/${SOCKETDIR} || :;

lint:
	for dir in lcr lib exec services tools test; do make -C $$dir lint; done

doxygen:
	mkdir -p doc/api && doxygen

dist-clean-local:
	rm -f autoconf automake autoheader

clean-generic:
	rm -rf doc/api $(SPEC) $(TARFILE) 

## make rpm/srpm section.

$(SPEC): $(SPEC).in
	rm -f $@-t $@
	date="$(shell LC_ALL=C date "+%a %b %d %Y")" && \
	if [ -f .tarball-version ]; then \
		gitver="$(shell cat .tarball-version)" && \
		rpmver=$$gitver && \
		alphatag="" && \
		dirty="" && \
		numcomm="0"; \
	else \
		gitver="$(shell git describe --abbrev=4 --match='v*' HEAD 2>/dev/null)" && \
		rpmver=`echo $$gitver | sed -e "s/^v//" -e "s/-.*//g"` && \
		alphatag=`echo $$gitver | sed -e "s/.*-//" -e "s/^g//"` && \
		vtag=`echo $$gitver | sed -e "s/-.*//g"` && \
		numcomm=`git rev-list $$vtag..HEAD | wc -l` && \
		git update-index --refresh > /dev/null 2>&1 || true && \
		dirty=`git diff-index --name-only HEAD 2>/dev/null`; \
	fi && \
	if [ -n "$$dirty" ]; then dirty="dirty"; else dirty=""; fi && \
	if [ "$$numcomm" = "0" ]; then \
		sed \
			-e "s#@version@#$$rpmver#g" \
			-e "s#%glo.*alpha.*##g" \
			-e "s#%glo.*numcomm.*##g" \
			-e "s#@dirty@#$$dirty#g" \
			-e "s#@date@#$$date#g" \
		$< > $@-t; \
	else \
		sed \
			-e "s#@version@#$$rpmver#g" \
			-e "s#@alphatag@#$$alphatag#g" \
			-e "s#@numcomm@#$$numcomm#g" \
			-e "s#@dirty@#$$dirty#g" \
			-e "s#@date@#$$date#g" \
		$< > $@-t; \
	fi; \
	if [ -z "$$dirty" ]; then sed -i -e "s#%glo.*dirty.*##g" $@-t; fi
	chmod a-w $@-t
	mv $@-t $@

$(TARFILE):
	$(MAKE) dist

RPMBUILDOPTS	= --define "_sourcedir $(abs_builddir)" \
		  --define "_specdir $(abs_builddir)" \
		  --define "_builddir $(abs_builddir)" \
		  --define "_srcrpmdir $(abs_builddir)" \
		  --define "_rpmdir $(abs_builddir)"

srpm: clean
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(RPMBUILDOPTS) --nodeps -bs $(SPEC)

rpm: clean
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(RPMBUILDOPTS) -ba $(SPEC)

# release/versioning
BUILT_SOURCES	= .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook: gen-ChangeLog
	echo $(VERSION) > $(distdir)/.tarball-version

gen_start_date = 2000-01-01
.PHONY: gen-ChangeLog
gen-ChangeLog:
	if test -d .git; then						\
		LC_ALL=C $(top_srcdir)/build-aux/gitlog-to-changelog		\
			--since=$(gen_start_date) > $(distdir)/cl-t;	\
		rm -f $(distdir)/ChangeLog;				\
		mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi
