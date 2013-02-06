#!@BASHPATH@

# Copyright (c) 2013 Red Hat, Inc.
#
# All rights reserved.
#
# Author: Jan Friesse (jfriesse@redhat.com)
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
# - Neither the name of the Red Hat, Inc. nor the names of its
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

SCHEMA="@DATADIR@/corosync/corosync.rng"
STYLESHEET="@DATADIR@/corosync/xml2conf.xsl"

XSLT_PROC=xsltproc
# to be run as (xmllint/jing compatible): $XML_VALIDATOR "$SCHEMA" "$1"
#XML_VALIDATOR="jing"
XML_VALIDATOR="xmllint --noout --relaxng"

out_param=
force=0

usage() {
    echo "$0 [-f] input_config [output]"
    echo "	where -f means to forcibly continue despite validation issues"
    echo "	and input_config is the XML formatted configuration file"

    exit 1
}

if [ "$1" = "-f" ]; then
    force=1
    shift
fi
[ "$1" = "" ] && usage

$XSLT_PROC -V >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "Can't find XSLT processor $XSLT_PROC"
    exit 2
fi

# stdout reserved for generated output
$XML_VALIDATOR "$SCHEMA" "$1" >&2
ret=$?
if [ $ret -ne 0 ]; then
    if [ $force -eq 1 ]; then
        echo "Continuing despite failing to validate ($ret)"
    elif [ $ret -eq 127 ]; then
        echo "Can't find XML validator $XML_VALIDATOR"
        exit 2
    else
        echo "Validation failed"
        exit 3
    fi
fi

[ "$2" != "" ] && out_param="-o $2"

$XSLT_PROC --stringparam inputfile "$1" $out_param "$STYLESHEET" "$1"
