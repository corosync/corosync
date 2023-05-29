#
# Copyright (C) 2022-2023 Red Hat, Inc.  All rights reserved.
#
# Author: Christine Caulfield <ccaulfie@redhat.com>
#
# This software licensed under GPL-2.0+
#

MAINTAINERCLEANFILES	= Makefile.in

include $(top_srcdir)/build-aux/rust.mk

# required for make check
localver		= $(corosyncrustver)

SUBDIRS			= . tests

EXTRA_DIST		= \
			  $(RUST_COMMON) \
			  $(RUST_SHIP_SRCS) \
			  README.md

RUST_SHIP_SRCS		= \
			  src/cpg.rs \
			  src/cfg.rs \
			  src/quorum.rs \
			  src/votequorum.rs \
			  src/cmap.rs \
			  src/lib.rs \
			  src/sys/mod.rs

RUST_BUILT_SRCS		= \
			  src/sys/cpg.rs \
			  src/sys/cfg.rs \
			  src/sys/quorum.rs \
			  src/sys/votequorum.rs \
			  src/sys/cmap.rs

src/sys/cpg.rs: cargo-tree-prep ../../include/corosync/cpg.h
	$(top_srcdir)/build-aux/rust-regen.sh $(top_srcdir)/include/corosync/cpg.h $@ CPG --blocklist-function=inet6.* --blocklist-function==.*etsourcefilter -- -I$(top_srcdir)/include

src/sys/cfg.rs: cargo-tree-prep ../../include/corosync/cfg.h
	$(top_srcdir)/build-aux/rust-regen.sh $(top_srcdir)/include/corosync/cfg.h $@ CFG --blocklist-function=inet6.* --blocklist-function=.*etsourcefilter -- -I$(top_srcdir)/include

src/sys/quorum.rs: cargo-tree-prep ../../include/corosync/quorum.h
	$(top_srcdir)/build-aux/rust-regen.sh $(top_srcdir)/include/corosync/quorum.h $@ QUORUM -- -I$(top_srcdir)/include

src/sys/votequorum.rs: cargo-tree-prep ../../include/corosync/votequorum.h
	$(top_srcdir)/build-aux/rust-regen.sh $(top_srcdir)/include/corosync/votequorum.h $@ VOTEQUORUM -- -I$(top_srcdir)/include

src/sys/cmap.rs: cargo-tree-prep ../../include/corosync/cmap.h
	$(top_srcdir)/build-aux/rust-regen.sh $(top_srcdir)/include/corosync/cmap.h $@ CMAP -- -I$(top_srcdir)/include $(LIBQB_CFLAGS)

all-local: target/$(RUST_TARGET_DIR)/cpg.rlib \
	target/$(RUST_TARGET_DIR)/cfg.rlib \
	target/$(RUST_TARGET_DIR)/quorum.rlib \
	target/$(RUST_TARGET_DIR)/votequorum.rlib \
	target/$(RUST_TARGET_DIR)/cmap.rlib

clean-local: cargo-clean
