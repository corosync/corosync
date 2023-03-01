#!/bin/sh
#
# Copyright (C) 2021-2023 Red Hat, Inc.  All rights reserved.
#
# Authors: Christine Caulfield <ccaulfie@redhat.com>
#          Fabio M. Di Nitto <fabbione@kronosnet.org>
#
# This software licensed under GPL-2.0+
#
#
# Regerate the FFI bindings in src/sys from the current headers
#

srcheader="$1"
dstrs="$2"
filter="$3"
shift; shift; shift

bindgen \
	--no-prepend-enum-name \
	--no-layout-tests \
	--no-doc-comments \
	--generate functions,types \
	--fit-macro-constant-types \
	--allowlist-var=$filter.*  \
	--allowlist-type=.* \
	--allowlist-function=.* \
	$srcheader -o $dstrs "$@"
