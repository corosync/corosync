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

EXTRA_DIST		= autogen.sh $(SPEC).in \
			  build-aux/git-version-gen \
			  build-aux/gitlog-to-changelog \
			  build-aux/release.mk \
			  build-aux/rust.mk \
			  build-aux/rust-regen.sh \
			  .version

ACLOCAL_AMFLAGS		= -I m4

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf test_lense.sh \
			  autoscan.log configure.scan ltmain.sh test-driver

dist_doc_DATA		= LICENSE INSTALL README.recovery AUTHORS

SUBDIRS			= include common_lib lib exec tools test pkgconfig \
			  man init conf vqsim bindings

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

coverity-aggressive:
	rm -rf cov
	make clean
	cov-build --dir=cov make
	cov-analyze --dir cov \
		    --concurrency \
		    --all \
		    --aggressiveness-level high \
		    --security \
		    --wait-for-license
	cov-format-errors --dir cov

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/${COROSYSCONFDIR}/service.d
	$(INSTALL) -d $(DESTDIR)/${COROSYSCONFDIR}/uidgid.d
	$(INSTALL) -d $(DESTDIR)/${localstatedir}/lib/corosync
	$(INSTALL) -d $(DESTDIR)/${localstatedir}/log/cluster

uninstall-local:
	rmdir $(DESTDIR)/${COROSYSCONFDIR}/service.d || :;
	rmdir $(DESTDIR)/${COROSYSCONFDIR}/uidgid.d || :;
	rmdir $(DESTDIR)/${localstatedir}/lib/corosync || :;
	rmdir $(DESTDIR)/${localstatedir}/log/cluster || :;

if AUGTOOL
check_SCRIPTS = test_lense.sh
TESTS = $(check_SCRIPTS)

test_lense.sh:
	echo "augparse -I $(srcdir)/conf/lenses/ $(srcdir)/conf/lenses/tests/test_corosync.aug" > $@
	chmod +x $@

endif

lint:
	for dir in lib exec tools test; do make -C $$dir lint; done

.PHONY: doxygen
doxygen:
	@if [ "$(DOXYGEN)" = "" ] || [ "$(DOT)" = "" ] ; then \
		echo "*********************************************" ; \
		echo "***                                       ***" ; \
		echo "*** You must install doxygen and graphviz ***" ; \
		echo "*** to generate the API documentation.    ***" ; \
		echo "***                                       ***" ; \
		echo "*********************************************" ; \
		exit 1 ; \
	else \
		mkdir -p doc/api && $(DOXYGEN) ; \
	fi

dist-clean-local:
	rm -f autoconf automake autoheader test_lense.sh

clean-generic:
	rm -rf doc/api $(SPEC) $(TARFILE) test_lense.sh

## make rpm/srpm section.

$(SPEC): $(SPEC).in
	rm -f $@-t $@
	date="$(shell LC_ALL=C date "+%a %b %d %Y")" && \
	gvgver="`cd $(abs_srcdir); build-aux/git-version-gen --fallback $(VERSION) .tarball-version .gitarchivever`" && \
	if [ "$$gvgver" = "`echo $$gvgver | sed 's/-/./'`" ];then \
		rpmver="$$gvgver" && \
		alphatag="" && \
		dirty="" && \
		numcomm="0"; \
	else \
		gitver="`echo $$gvgver | sed 's/\(.*\)\./\1-/'`" && \
		rpmver=`echo $$gitver | sed 's/-.*//g'` && \
		alphatag=`echo $$gvgver | sed 's/[^-]*-\([^-]*\).*/\1/'` && \
		numcomm=`echo $$gitver | sed 's/[^-]*-\([^-]*\).*/\1/'` && \
		dirty="" && \
		if [ "`echo $$gitver | sed 's/^.*-dirty$$//g'`" = "" ];then \
			dirty="dirty"; \
		fi \
	fi && \
	if [ -n "$$dirty" ]; then dirty="dirty"; else dirty=""; fi && \
	if [ "$$numcomm" = "0" ]; then numcomm=""; fi && \
	if [ -n "$$numcomm" ]; then numcomm="%global numcomm $$numcomm"; fi && \
	if [ "$$alphatag" = "$$gitver" ]; then alphatag=""; fi && \
	if [ -n "$$alphatag" ]; then alphatag="%global alphatag $$alphatag"; fi && \
	if [ -n "$$dirty" ]; then dirty="%global dirty dirty"; fi && \
	$(SED) \
		-e "s#@version@#$$rpmver#g" \
		-e "s#@ALPHATAG@#$$alphatag#g" \
		-e "s#@NUMCOMM@#$$numcomm#g" \
		-e "s#@DIRTY@#$$dirty#g" \
		-e "s#@date@#$$date#g" \
	$< > $@-t; \
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
	rpmbuild $(WITH_LIST) $(RPMBUILDOPTS) --nodeps -bs $(SPEC)

rpm: clean _version
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(WITH_LIST) $(RPMBUILDOPTS) -ba $(SPEC)

# release/versioning
BUILT_SOURCES	= .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook: gen-ChangeLog
	echo $(VERSION) > $(distdir)/.tarball-version

gen_start_date = 2000-01-01
.PHONY: gen-ChangeLog _version
gen-ChangeLog:
	if test -d .git; then						\
		LC_ALL=C $(top_srcdir)/build-aux/gitlog-to-changelog		\
			--since=$(gen_start_date) > $(distdir)/cl-t;	\
		rm -f $(distdir)/ChangeLog;				\
		mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi

_version:
	cd $(srcdir) && rm -rf autom4te.cache .version && autoreconf -i
	$(MAKE) $(AM_MAKEFLAGS) Makefile

maintainer-clean-local:
	rm -rf m4
