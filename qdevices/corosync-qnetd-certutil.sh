#!@BASHPATH@

#
# Copyright (c) 2015-2016 Red Hat, Inc.
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
#

CONFIG_DIR="@COROSYSCONFDIR@/qnetd"
DB_DIR="$CONFIG_DIR/nssdb"
# Validity of certificate (months)
CRT_VALIDITY=1200
CA_NICKNAME="QNet CA"
SERVER_NICKNAME="QNetd Cert"
CLUSTER_NICKNAME="Cluster Cert"
CA_SUBJECT="CN=QNet CA"
SERVER_SUBJECT="CN=Qnetd Server"
PWD_FILE="$DB_DIR/pwdfile.txt"
NOISE_FILE="$DB_DIR/noise.txt"
SERIAL_NO_FILE="$DB_DIR/serial.txt"
CA_EXPORT_FILE="$DB_DIR/qnetd-cacert.crt"
CRT_FILE_BASE="" # Generated from cluster name

usage() {
    echo "$0: [-i|-s] [-c certificate] [-n cluster_name]"
    echo
    echo " -i                  Initialize QNetd CA and generate server certificate"
    echo " -s                  Sign cluster certificate (needs cluster certificate)"
    echo " -c certificate      CRQ certificate file name"
    echo " -n cluster_name     Name of cluster (for -s operation)"

    exit 0
}

chown_ref_cfgdir() {
    if [ "$UID" == "0" ];then
        chown --reference="$CONFIG_DIR" "$@" 2>/dev/null || chown `stat -f "%u:%g" "$CONFIG_DIR"` "$@" 2>/dev/null || return $?
    fi
}

create_new_noise_file() {
    local noise_file="$1"

    if [ ! -e "$noise_file" ];then
        echo "Creating new noise file $noise_file"

        (ps -elf; date; w) | sha1sum | (read sha_sum rest; echo $sha_sum) > "$noise_file"

        chown_ref_cfgdir "$noise_file"
        chmod 0660 "$noise_file"
    else
        echo "Using existing noise file $noise_file"
    fi
}

get_serial_no() {
    local serial_no

    if ! [ -f "$SERIAL_NO_FILE" ];then
        echo "100" > $SERIAL_NO_FILE
        chown_ref_cfgdir "$SERIAL_NO_FILE"
        chmod 0660 "$SERIAL_NO_FILE"
    fi
    serial_no=`cat $SERIAL_NO_FILE`
    serial_no=$((serial_no+1))
    echo "$serial_no" > $SERIAL_NO_FILE
    echo "$serial_no"
}

init_qnetd_ca() {
    if [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database ($DB_DIR) already exists. Delete it to initialize new db" >&2

        exit 1
    fi

    if ! [ -d "$DB_DIR" ];then
        echo "Creating $DB_DIR"
        mkdir -p "$DB_DIR"
        chown_ref_cfgdir "$DB_DIR"
        chmod 0770 "$DB_DIR"
    fi

    echo "Creating new key and cert db"
    echo -n "" > "$PWD_FILE"
    chown_ref_cfgdir "$PWD_FILE"
    chmod 0660 "$PWD_FILE"

    certutil -N -d "$DB_DIR" -f "$PWD_FILE"
    chown_ref_cfgdir "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"
    chmod 0660 "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"

    create_new_noise_file "$NOISE_FILE"

    echo "Creating new CA"
    # Create self-signed certificate (CA). Asks 3 questions (is this CA, lifetime and critical extension
    echo -e "y\n0\ny\n" | certutil -S -n "$CA_NICKNAME" -s "$CA_SUBJECT" -x \
        -t "CT,," -m `get_serial_no` -v $CRT_VALIDITY -d "$DB_DIR" \
        -z "$NOISE_FILE" -f "$PWD_FILE" -2
    # Export CA certificate in ascii
    certutil -L -d "$DB_DIR" -n "$CA_NICKNAME" > "$CA_EXPORT_FILE"
    certutil -L -d "$DB_DIR" -n "$CA_NICKNAME" -a >> "$CA_EXPORT_FILE"
    chown_ref_cfgdir "$CA_EXPORT_FILE"

    certutil -S -n "$SERVER_NICKNAME" -s "$SERVER_SUBJECT" -c "$CA_NICKNAME" -t "u,u,u" -m `get_serial_no` \
        -v $CRT_VALIDITY -d "$DB_DIR" -z "$NOISE_FILE" -f "$PWD_FILE"

    echo "QNetd CA certificate is exported as $CA_EXPORT_FILE"
}


sign_cluster_cert() {
    if ! [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database doesn't exists. Use $0 -I to create it" >&2

        exit 1
    fi

    echo "Signing cluster certificate"
    certutil -C -v "$CRT_VALIDITY" -m `get_serial_no` -i "$CERTIFICATE_FILE" -o "$CRT_FILE" -c "$CA_NICKNAME" -d "$DB_DIR"
    chown_ref_cfgdir "$CRT_FILE"

    echo "Certificate stored in $CRT_FILE"
}


OPERATION=""
CERTIFICATE_FILE=""
CLUSTER_NAME=""

while getopts ":hisc:n:" opt; do
    case $opt in
        i)
            OPERATION=init_qnetd_ca
            ;;
        s)
            OPERATION=sign_cluster_cert
            ;;
        h)
            usage
            ;;
        c)
            CERTIFICATE_FILE="$OPTARG"
            ;;
        n)
            CLUSTER_NAME="$OPTARG"
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2

            exit 1
            ;;
        :)
            echo "Option -$OPTARG requires an argument." >&2

            exit 1
            ;;
   esac
done

[ "$OPERATION" == "" ] && usage

CRT_FILE="$DB_DIR/cluster-$CLUSTER_NAME.crt"

case "$OPERATION" in
    "init_qnetd_ca")
        init_qnetd_ca
    ;;
    "sign_cluster_cert")
        if ! [ -e "$CERTIFICATE_FILE" ];then
            echo "Can't open certificate file $CERTIFICATE_FILE" >&2

            exit 2
        fi

        if [ "$CLUSTER_NAME" == "" ];then
            echo "You have to specify cluster name" >&2

            exit 2
        fi

        sign_cluster_cert
    ;;
    *)
        usage
    ;;
esac
