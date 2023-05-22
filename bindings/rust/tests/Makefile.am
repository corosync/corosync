#
# Copyright (C) 2022-2023 Red Hat, Inc.  All rights reserved.
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
			  src/bin/cmap-test.rs \
			  src/bin/quorum-test.rs \
			  src/bin/votequorum-test.rs

# This will build all of the tests
noinst_SCRIPTS		= target/$(RUST_TARGET_DIR)/cpg-test

clean-local: cargo-clean
