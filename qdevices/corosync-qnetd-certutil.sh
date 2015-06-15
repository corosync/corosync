#!/bin/bash

#
# Copyright (c) 2015 Red Hat, Inc.
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

DB_DIR=nssdb
# Validity of certificate (months)
CRT_VALIDITY=120
CA_NICKNAME="QNet CA"
SERVER_NICKNAME="QNetd Cert"
CLUSTER_NICKNAME="Cluster Cert"
CA_SUBJECT="CN=QNet CA"
SERVER_SUBJECT="CN=Qnetd Server"
PWD_FILE="$DB_DIR/pwdfile.txt"
NOISE_FILE="$DB_DIR/noise.txt"
SERIAL_NO_FILE="$DB_DIR/serial.txt"
CA_EXPORT_FILE="$DB_DIR/qnetd-cacert.crt"
CRQ_FILE="$DB_DIR/qnetd-node.crq"
CRT_FILE="" # Generated from cluster name
P12_FILE="$DB_DIR/qnetd-node.p12"

usage() {
    echo "$0: [-I|-i|-m|-M|-r|-s] [-c certificate] [-n cluster_name]"
    echo
    echo " -I      Initialize server CA and generate server certificate"
    echo " -i      Initialize node CA. Needs CA certificate from server"
    echo ""
    echo " -m      Import cluster certificate on node (needs pk12 certificate)"
    echo ""
    echo " -r      Generate cluster certificate request"
    echo " -s      Sign cluster certificate on qnetd server (needs cluster certificate)"
    echo " -M      Import signed cluster certificate and export certificate with key to pk12 file"
    echo ""
    echo " -c certificate      Ether CA, CRQ, CRT or pk12 certificate (operation dependant)"
    echo " -n cluster_name     Name of cluster (for -r and -s operations)"
    echo ""
    echo "Typical usage:"
    echo "- Initialize database on QNetd server by running $0 -I"
    echo "- Copy exported QNetd CA certificate ($CA_EXPORT_FILE) to every node"
    echo "- On one of cluster node initialize database by running $0 -i -c `basename $CA_EXPORT_FILE`"
    echo "- Generate certificate request: $0 -r -n Cluster"
    echo "- Copy exported CRQ to QNetd server"
    echo "- On QNetd server sign and export cluster certificate by running $0 -s -c `basename $CRQ_FILE` -n Cluster"
    echo "- Copy exported CRT to node where certificate request was created"
    echo "- Import certificate on node where certificate request was created by running $0 -M -c qnetd-cluster-Cluster.crt"
    echo "- Copy output $P12_FILE to all other cluster nodes"
    echo "- On all other nodes in cluster:"
    echo "  - Init database by running $0 -i -c `basename $CA_EXPORT_FILE`"
    echo "  - Import cluster certificate and key: $0 -m -c `basename $P12_FILE`"

    exit 0
}

create_new_noise_file() {
    local noise_file="$1"

    if [ ! -e "$noise_file" ];then
        echo "Creating new noise file $noise_file"

        (ps -elf; date; w) | sha1sum | (read sha_sum rest; echo $sha_sum) > "$noise_file"

        chown root:root "$noise_file"
        chmod 400 "$noise_file"
    else
        echo "Using existing noise file $noise_file"
    fi
}

get_serial_no() {
    local serial_no

    if ! [ -f "$SERIAL_NO_FILE" ];then
        echo "100" > $SERIAL_NO_FILE
    fi
    serial_no=`cat $SERIAL_NO_FILE`
    serial_no=$((serial_no+1))
    echo "$serial_no" > $SERIAL_NO_FILE
    echo "$serial_no"
}

init_server_ca() {
    if [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database already exists. Delete it to continue" >&2

        exit 1
    fi

    if ! [ -d "$DB_DIR" ];then
        echo "Creating $DB_DIR"
        mkdir "$DB_DIR"
        chown root:root "$DB_DIR"
        chmod 700 "$DB_DIR"
    fi

    echo "Creating new key and cert db"
    echo -n "" > "$PWD_FILE"
    certutil -N -d "$DB_DIR" -f "$PWD_FILE"
    chown root:root "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"
    chmod 600 "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"

    create_new_noise_file "$NOISE_FILE"

    echo "Creating new CA"
    # Create self-signed certificate (CA). Asks 3 questions (is this CA, lifetime and critical extension
    echo -e "y\n0\ny\n" | certutil -S -n "$CA_NICKNAME" -s "$CA_SUBJECT" -x \
        -t "CT,," -m `get_serial_no` -v $CRT_VALIDITY -d "$DB_DIR" \
        -z "$NOISE_FILE" -f "$PWD_FILE" -2
    # Export CA certificate in ascii
    certutil -L -d "$DB_DIR" -n "$CA_NICKNAME" > "$CA_EXPORT_FILE"
    certutil -L -d "$DB_DIR" -n "$CA_NICKNAME" -a >> "$CA_EXPORT_FILE"

    certutil -S -n "$SERVER_NICKNAME" -s "$SERVER_SUBJECT" -c "$CA_NICKNAME" -t "u,u,u" -m `get_serial_no` \
        -v $CRT_VALIDITY -d "$DB_DIR" -z "$NOISE_FILE" -f "$PWD_FILE"

    echo "QNetd CA is exported as $CA_EXPORT_FILE"
}

init_node_ca() {
    if [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database already exists. Delete it to continue" >&2

        exit 1
    fi

    if ! [ -d "$DB_DIR" ];then
        echo "Creating $DB_DIR"
        mkdir "$DB_DIR"
        chown root:root "$DB_DIR"
        chmod 700 "$DB_DIR"
    fi

    echo "Creating new key and cert db"
    echo -n "" > "$PWD_FILE"
    certutil -N -d "$DB_DIR" -f "$PWD_FILE"
    chown root:root "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"
    chmod 600 "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"

    create_new_noise_file "$NOISE_FILE"

    echo "Importing CA"

    certutil -d "$DB_DIR" -A -t "CT,c,c" -n "$CA_NICKNAME" -f "$PWD_FILE" \
        -i "$CERTIFICATE_FILE"
}

gen_cluster_cert_req() {
    if ! [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database doesn't exists. Use $0 -i to create it" >&2

        exit 1
    fi

    echo "Creating new certificate request"

    certutil -R -s "CN=$CLUSTER_NAME" -o "$CRQ_FILE" -d "$DB_DIR" -f "$PWD_FILE" -z "$NOISE_FILE"

    echo "Certificate request stored in $CRQ_FILE"
}

sign_cluster_cert() {
    if ! [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database doesn't exists. Use $0 -I to create it" >&2

        exit 1
    fi

    echo "Signing cluster certificate"
    certutil -C -m `get_serial_no` -i "$CERTIFICATE_FILE" -o "$CRT_FILE" -c "$CA_NICKNAME" -d "$DB_DIR"

    echo "Certificate stored in $CRT_FILE"
}

import_signed_cert() {
    if ! [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database doesn't exists. Use $0 -i to create it" >&2

        exit 1
    fi

    echo "Importing signed cluster certificate"
    certutil -d "$DB_DIR" -A -t "u,u,u" -n "$CLUSTER_NICKNAME" -i "$CERTIFICATE_FILE"

    pk12util -d "$DB_DIR" -o "$P12_FILE" -W "" -n "$CLUSTER_NICKNAME"

    echo "Certificate stored in $P12_FILE"
}

import_pk12() {
    if ! [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database doesn't exists. Use $0 -i to create it" >&2

        exit 1
    fi

    echo "Importing cluster certificate and key"
    pk12util -i "$CERTIFICATE_FILE" -d "$DB_DIR" -W ""
}

OPERATION=""
CERTIFICATE_FILE=""
CLUSTER_NAME=""

while getopts ":hIiMmrsc:n:" opt; do
    case $opt in
        r)
            OPERATION=gen_cluster_cert_req
            ;;
        I)
            OPERATION=init_server_ca
            ;;
        i)
            OPERATION=init_node_ca
            ;;
        s)
            OPERATION=sign_cluster_cert
            ;;
        m)
            OPERATION=import_pk12
            ;;
        M)
            OPERATION=import_signed_cert
            ;;
        n)
            CLUSTER_NAME="$OPTARG"
            ;;
        h)
            usage
            ;;
        c)
            CERTIFICATE_FILE="$OPTARG"
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

case "$OPERATION" in
    "init_server_ca")
        init_server_ca
    ;;
    "init_node_ca")
        if ! [ -e "$CERTIFICATE_FILE" ];then
            echo "Can't open certificate file $CERTIFICATE_FILE" >&2

            exit 2
        fi

        init_node_ca
    ;;
    "gen_cluster_cert_req")
        if [ "$CLUSTER_NAME" == "" ];then
            echo "You have to specify cluster name" >&2

            exit 2
        fi

        gen_cluster_cert_req
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

        CRT_FILE="$DB_DIR/qnetd-cluster-$CLUSTER_NAME.crt"
        sign_cluster_cert
    ;;
    "import_signed_cert")
        if ! [ -e "$CERTIFICATE_FILE" ];then
            echo "Can't open certificate file $CERTIFICATE_FILE" >&2

            exit 2
        fi

        import_signed_cert
    ;;
    "import_pk12")
        if ! [ -e "$CERTIFICATE_FILE" ];then
            echo "Can't open certificate file $CERTIFICATE_FILE" >&2

            exit 2
        fi

        import_pk12
    ;;
    *)
        usage
    ;;
esac
