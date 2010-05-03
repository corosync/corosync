#!/bin/sh

set -e

if [ $1 = "BreakCommCmd" ]
then
  iptables -A INPUT -s $2 -j DROP >/dev/null 2>&1
  iptables -A OUTPUT -s $2 -j DROP >/dev/null 2>&1
  iptables -A INPUT -m pkttype --pkt-type multicast -j DROP
fi
if [ $1 = "FixCommCmd" ]
then
  iptables -F >/dev/null 2>&1
fi

exit 0

