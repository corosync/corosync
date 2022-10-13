#
# Copyright (C) 2021-2022 Red Hat, Inc.  All rights reserved.
#
# Author: Fabio M. Di Nitto <fabbione@kronosnet.org>
#
# This software licensed under GPL-2.0+
#

RUST_COMMON = \
	      build.rs.in

RUST_SRCS = $(RUST_SHIP_SRCS) $(RUST_BUILT_SRCS)

%.rlib: $(RUST_SRCS) Cargo.toml build.rs
	PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) $(CARGO) build $(RUST_FLAGS)

%-test: $(RUST_SRCS) Cargo.toml build.rs
	PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) $(CARGO) build $(RUST_FLAGS)

build.rs: build.rs.in
	rm -f $@ $@-t
	cat $^ | sed \
		-e 's#@ABSTOPLEVELSRC@#$(abs_top_srcdir)#g' \
		-e 's#@ABSTOPLEVELBUILD@#$(abs_top_builddir)#g' \
		> $@-t
	chmod a-w $@-t
	mv $@-t $@
	rm -f $@-t

cargo-tree-prep:
	if [ "${abs_builddir}" != "${abs_srcdir}" ]; then \
		echo "Generating builddir out-of-tree rust symlinks"; \
		src_realpath=$(shell realpath ${abs_srcdir}); \
		for i in `find "$$src_realpath/" -type d | \
			grep -v "${abs_builddir}" | \
			sed -e 's#^'$$src_realpath'/##g'`; do \
			$(MKDIR_P) ${abs_builddir}/$${i}; \
		done; \
		find "$$src_realpath/" -type f | { while read src; do \
			process=no; \
			copy=no; \
			case $$src in \
				${abs_builddir}*) \
					;; \
				*Makefile.*|*.in) \
					;; \
				*) \
					process=yes; \
					;; \
			esac ; \
			dst=`echo $$src | sed -e 's#^'$$src_realpath'/##g'`; \
			if [ $${process} == yes ]; then \
				rm -f ${abs_builddir}/$$dst; \
				$(LN_S) $$src ${abs_builddir}/$$dst; \
			fi; \
			if [ $${copy} == yes ]; then \
				rm -f ${abs_builddir}/$$dst; \
				cp $$src ${abs_builddir}/$$dst; \
				chmod u+w ${abs_builddir}/$$dst; \
			fi; \
		done; }; \
	fi

cargo-clean:
	-$(CARGO) clean
	rm -rf Cargo.lock $(RUST_BUILT_SRCS) build.rs target/
	if [ "${abs_builddir}" != "${abs_srcdir}" ]; then \
		echo "Cleaning out-of-tree rust symlinks" ; \
		find "${abs_builddir}/" -type l -delete; \
		find "${abs_builddir}/" -type d -empty -delete; \
	fi

clippy-check:
	$(CARGO) clippy --verbose --all-features -- -D warnings

doc-check:
	$(CARGO) doc --verbose --all-features

publish-check:
	if [ -f "${abs_srcdir}/README" ]; then \
		$(CARGO) publish --dry-run; \
	fi

crates-publish:
	if [ -f "${abs_srcdir}/README" ]; then \
		bindingname=`cat Cargo.toml | grep ^name | sed -e 's#.*= ##g' -e 's#"##g'` && \
		cratesver=`cargo search $$bindingname | grep "^$$bindingname " | sed -e 's#.*= ##g' -e 's#"##g' -e 's/\+.*//g'` && \
		testver=`echo $(localver) | sed -e 's/\+.*//g'` && \
		if [ "$$cratesver" != "$$testver" ]; then \
			$(CARGO) publish; \
		fi; \
	fi

crates-check:
	if [ -f "${abs_srcdir}/README" ]; then \
		bindingname=`cat Cargo.toml | grep ^name | sed -e 's#.*= ##g' -e 's#"##g'` && \
		cratesver=`cargo search $$bindingname | grep "^$$bindingname " | sed -e 's#.*= ##g' -e 's#"##g' -e 's/\+.*//g'` && \
		testver=`echo $(localver) | sed -e 's/\+.*//g'` && \
		if [ "$$cratesver" != "$$testver" ]; then \
			echo "!!!!! WARNING !!!!!"; \
			echo "!!!!! WARNING: $$bindingname local version ($$testver) is higher than the current published one on crates.io ($$cratesver)"; \
			echo "!!!!! WARNING !!!!!"; \
		fi; \
	fi

check-local: clippy-check doc-check crates-check publish-check
