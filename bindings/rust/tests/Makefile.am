#
# Copyright (C) 2021-2022 Red Hat, Inc.  All rights reserved.
#
# Author: Christine Caulfield <ccaulfie@redhat.com>
#
# This software licensed under GPL-2.0+
#

MAINTAINERCLEANFILES	= Makefile.in

include $(top_srcdir)/build-aux/rust.mk

EXTRA_DIST		= \
			  $(RUST_COMMON) \
			  $(RUST_SHIP_SRCS)

RUST_SHIP_SRCS		= src/bin/cpg-test.rs \
			  src/bin/cfg-test.rs \
			  src/bin/quorum-test.rs \
			  src/bin/votequorum-test.rs \
			  src/bin/cfg-test.rs

# This will build all of the tests
check_SCRIPTS		= target/$(RUST_TARGET_DIR)/cpg-test

noinst_SCRIPTS		= $(check_SCRIPTS)

AM_TESTS_ENVIRONMENT=LD_LIBRARY_PATH="$(abs_top_builddir)/lib/.libs"

TESTS			= $(check_SCRIPTS)

check:
	cargo clippy --all-features
	cargo fmt --all --check

clean-local: cargo-clean
