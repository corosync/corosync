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

BASE_DIR="@COROSYSCONFDIR@/qdevice/net"
DB_DIR_QNETD="@COROSYSCONFDIR@/qnetd/nssdb"
DB_DIR_NODE="$BASE_DIR/nssdb"
# Validity of certificate (months)
CRT_VALIDITY=1200
CA_NICKNAME="QNet CA"
SERVER_NICKNAME="QNetd Cert"
CLUSTER_NICKNAME="Cluster Cert"
CA_SUBJECT="CN=QNet CA"
SERVER_SUBJECT="CN=Qnetd Server"
PWD_FILE_BASE="pwdfile.txt"
NOISE_FILE_BASE="noise.txt"
SERIAL_NO_FILE_BASE="serial.txt"
CA_EXPORT_FILE="$DB_DIR_QNETD/qnetd-cacert.crt"
CRQ_FILE_BASE="qdevice-net-node.crq"
CRT_FILE_BASE="" # Generated from cluster name
P12_FILE_BASE="qdevice-net-node.p12"
QNETD_CERTUTIL_CMD="corosync-qnetd-certutil"

usage() {
    echo "$0: [-i|-m|-M|-r|-s|-Q] [-c certificate] [-n cluster_name]"
    echo
    echo " -i      Initialize node CA. Needs CA certificate from server"
    echo " -m      Import cluster certificate on node (needs pk12 certificate)"
    echo " -r      Generate cluster certificate request"
    echo " -M      Import signed cluster certificate and export certificate with key to pk12 file"
    echo " -Q      Quick start. Uses ssh/scp to initialze both qnetd and nodes."
    echo ""
    echo " -c certificate      Ether CA, CRQ, CRT or pk12 certificate (operation dependant)"
    echo " -n cluster_name     Name of cluster (for -r and -s operations)"
    echo ""
    echo "Typical usage:"
    echo "- Initialize database on QNetd server by running $QNETD_CERTUTIL_CMD -i"
    echo "- Copy exported QNetd CA certificate ($CA_EXPORT_FILE) to every node"
    echo "- On one of cluster node initialize database by running $0 -i -c `basename $CA_EXPORT_FILE`"
    echo "- Generate certificate request: $0 -r -n Cluster (Cluster name must match cluster_name key in the corosync.conf)"
    echo "- Copy exported CRQ to QNetd server"
    echo "- On QNetd server sign and export cluster certificate by running $QNETD_CERTUTIL_CMD -s -c `basename $CRQ_FILE_BASE` -n Cluster"
    echo "- Copy exported CRT to node where certificate request was created"
    echo "- Import certificate on node where certificate request was created by running $0 -M -c cluster-Cluster.crt"
    echo "- Copy output $P12_FILE_BASE to all other cluster nodes"
    echo "- On all other nodes in cluster:"
    echo "  - Init database by running $0 -i -c `basename $CA_EXPORT_FILE`"
    echo "  - Import cluster certificate and key: $0 -m -c `basename $P12_FILE_BASE`"
    echo ""
    echo "It is also possible to use Quick start (-Q). This needs properly configured ssh."
    echo "  $0 -Q -n Cluster qnetd_server node1 node2 ... nodeN"

    exit 0
}

create_new_noise_file() {
    local noise_file="$1"

    if [ ! -e "$noise_file" ];then
        echo "Creating new noise file $noise_file"

        (ps -elf; date; w) | sha1sum | (read sha_sum rest; echo $sha_sum) > "$noise_file"

        chown root:root "$noise_file"
        chmod 0660 "$noise_file"
    else
        echo "Using existing noise file $noise_file"
    fi
}

get_serial_no() {
    local serial_no

    if ! [ -f "$SERIAL_NO_FILE" ];then
        echo "100" > $SERIAL_NO_FILE
        chown root:root "$DB_DIR"
        chmod 0660 "$SERIAL_NO_FILE"
    fi
    serial_no=`cat $SERIAL_NO_FILE`
    serial_no=$((serial_no+1))
    echo "$serial_no" > $SERIAL_NO_FILE
    echo "$serial_no"
}

init_node_ca() {
    if [ -f "$DB_DIR/cert8.db" ];then
        echo "Certificate database already exists. Delete it to continue" >&2

        exit 1
    fi

    if ! [ -d "$DB_DIR" ];then
        echo "Creating $DB_DIR"
        mkdir -p "$DB_DIR"
        chown root:root "$DB_DIR"
        chmod 0770 "$DB_DIR"
    fi

    echo "Creating new key and cert db"
    echo -n "" > "$PWD_FILE"
    chown root:root "$PWD_FILE"
    chmod 0660 "$PWD_FILE"
    certutil -N -d "$DB_DIR" -f "$PWD_FILE"
    chown root:root "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"
    chmod 0660 "$DB_DIR/key3.db" "$DB_DIR/cert8.db" "$DB_DIR/secmod.db"

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

quick_start() {
    qnetd_addr="$1"
    master_node="$2"
    other_nodes="$3"

    # Sanity check
    for i in "$master_node" $other_nodes;do
        if ssh root@$i "[ -d \"$DB_DIR_NODE\" ]";then
            echo "Node $i seems to be already initialized. Please delete $DB_DIR_NODE" >&2

            exit 1
        fi

        if ! ssh "root@$i" "$0" > /dev/null;then
            echo "Node $i doesn't have $0 installed" >&2

            exit 1
        fi
    done

    # Initialize qnetd server (it's no problem if server is already initialized)
    ssh "root@$qnetd_addr" "$QNETD_CERTUTIL_CMD -i"

    # Copy CA cert to all nodes and initialize them
    for node in "$master_node" $other_nodes;do
        scp "root@$qnetd_addr:$CA_EXPORT_FILE" "$node:/tmp"
        ssh "root@$node" "$0 -i -c \"/tmp/`basename $CA_EXPORT_FILE`\" && rm /tmp/`basename $CA_EXPORT_FILE`"
    done

    # Generate cert request
    ssh "root@$master_node" "$0 -r -n \"$CLUSTER_NAME\""

    # Copy exported cert request to qnetd server
    scp "root@$master_node:$DB_DIR_NODE/$CRQ_FILE_BASE" "root@$qnetd_addr:/tmp"

    # Sign and export cluster certificate
    ssh "root@$qnetd_addr" "$QNETD_CERTUTIL_CMD -s -c \"/tmp/$CRQ_FILE_BASE\" -n \"$CLUSTER_NAME\""

    # Copy exported CRT to master node
    scp "root@$qnetd_addr:$DB_DIR_QNETD/cluster-$CLUSTER_NAME.crt" "root@$master_node:$DB_DIR_NODE"

    # Import certificate
    ssh "root@$master_node" "$0 -M -c \"$DB_DIR_NODE/cluster-$CLUSTER_NAME.crt\""

    # Copy pk12 cert to all nodes and import it
    for node in $other_nodes;do
        scp "root@$master_node:$DB_DIR_NODE/$P12_FILE" "$node:$DB_DIR_NODE/$P12_FILE"
        ssh "root@$node" "$0 -m -c \"$DB_DIR_NODE/$P12_FILE\""
    done
}

OPERATION=""
CERTIFICATE_FILE=""
CLUSTER_NAME=""

while getopts ":hiMmQrc:n:" opt; do
    case $opt in
        r)
            OPERATION=gen_cluster_cert_req
            ;;
        i)
            OPERATION=init_node_ca
            ;;
        m)
            OPERATION=import_pk12
            ;;
        M)
            OPERATION=import_signed_cert
            ;;
        Q)
            OPERATION=quick_start
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
    "init_qnetd_ca")
        DB_DIR="$DB_DIR_QNETD"
    ;;
    "init_node_ca")
        DB_DIR="$DB_DIR_NODE"
    ;;
    "gen_cluster_cert_req")
        DB_DIR="$DB_DIR_NODE"
    ;;
    "sign_cluster_cert")
        DB_DIR="$DB_DIR_QNETD"
    ;;
    "import_signed_cert")
        DB_DIR="$DB_DIR_NODE"
    ;;
    "import_pk12")
        DB_DIR="$DB_DIR_NODE"
    ;;
    "quick_start")
        DB_DIR=""
    ;;
    *)
        usage
    ;;
esac

PWD_FILE="$DB_DIR/$PWD_FILE_BASE"
NOISE_FILE="$DB_DIR/$NOISE_FILE_BASE"
SERIAL_NO_FILE="$DB_DIR/$SERIAL_NO_FILE_BASE"
CRQ_FILE="$DB_DIR/$CRQ_FILE_BASE"
CRT_FILE="$DB_DIR/cluster-$CLUSTER_NAME.crt"
P12_FILE="$DB_DIR/$P12_FILE_BASE"

case "$OPERATION" in
    "init_qnetd_ca")
        init_qnetd_ca
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
    "quick_start")
        shift $((OPTIND-1))

        qnetd_addr="$1"

        shift 1

        master_node="$1"
        shift 1
        other_nodes="$@"

        if [ "$CLUSTER_NAME" == "" ];then
            echo "You have to specify cluster name" >&2

            exit 2
        fi

        if [ "$qnetd_addr" == "" ];then
            echo "No QNetd server address provided." >&2

            exit 2
        fi

        if [ "$master_node" == "" ];then
            echo "No nodes provided." >&2

            exit 2
        fi

        quick_start "$qnetd_addr" "$master_node" "$other_nodes"
    ;;
    *)
        usage
    ;;
esac
