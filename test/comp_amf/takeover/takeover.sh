#!/bin/sh
INTERFACE=$1
ADRESSE=$2
ACTION=$3

case "$ACTION" in
  start)
    /sbin/ifconfig $INTERFACE $ADRESSE
    DEVICE=`echo $INTERFACE | /usr/bin/cut -d ":" -f 1`
    LISTE=`/sbin/arp -i $DEVICE | /usr/bin/grep -v Address | /usr/bin/awk '{print $1}'`
    for elt in $LISTE; do
      /sbin/arping -q -c 1 -w 0 $elt &
    done
    ;;
  stop)
    (/sbin/ifconfig -a | grep $INTERFACE 1>/dev/null) && /sbin/ifconfig $INTERFACE down
    ;;
  *)
    echo "$0 ethx:y X.Y.Z.W [start|stop]"
    ;;
esac
