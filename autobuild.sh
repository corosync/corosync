#!/bin/sh
#
# This script is called by auto-build to test
# corosync. It is run continously to help catch regressions.
#
# ENVIRONMENT variables that affect it's behaviour:
#
# TEST_NODES - the hostnames of the nodes to be tested
# TARGET - this is used by mock so look in /etc/mock for
#          possible options.
#

# required packages
which mock >/dev/null 2>&1
if [ $? -ne 0 ]
then
	echo 'please install mock (yum install mock).'
        exit 1
fi

MOCK=/usr/bin/mock

set -e

echo 'running autogen ...'
./autogen.sh

echo 'running configure ...'
./configure 

echo 'building source rpm'
rm -f *.src.rpm
make srpm 
SRPM=$(ls *src.rpm)

if [ ! -f $SRPM ]
then
	echo $0 no source rpm to build from!
	exit 1
fi

if [ -z "$TARGET" ]
then
	TARGET=fedora-12-x86_64
fi

RPM_DIR=/var/lib/mock/$TARGET/result
rm -f $RPM_DIR/corosync*.rpm

echo "running mock init ($TARGET)"
$MOCK -r $TARGET --init 
echo "running mock rebuild ($SRPM)"
$MOCK -v -r $TARGET --rebuild $SRPM --with testagents

if [ -z "$TEST_NODES" ]
then
	echo no test nodes, exiting without running cts.
	exit 0
else
	# start the VMs, or leave them running?
	true
fi

RPM_LIST=
for r in $RPM_DIR/corosync*.rpm
do
  case $r in
    *src.rpm)
    ;;
    *-devel-*)
    ;;
#    *debuginfo*)
#    ;;
    *)
    RPM_LIST="$RPM_LIST $r"
    ;;
  esac
done


echo installing $RPM_LIST
echo onto the test nodes $TEST_NODES

# load and install rpm(s) onto the nodes
for n in $TEST_NODES
do
	ssh $n "rm -rf /tmp/corosync*.rpm"
	ssh $n "rm -f /etc/corosync/corosync.conf.*"
	scp $RPM_LIST $n:/tmp/
        ssh $n "rpm --force -Uvf /tmp/corosync*.rpm"
done

echo 'running test ...'
rm -f cts.log
pushd cts
# needs sudo to read /var/log/messages
sudo -n ./corolab.py --nodes "$TEST_NODES" --outputfile ../cts.log
popd

