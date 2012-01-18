#!/bin/sh
#
# This script is called by buildbot to test
# corosync. It is run continously to help catch regressions.
#
# ENVIRONMENT variables that affect it's behaviour:
#
# TEST_NODES - the hostnames of the nodes to be tested
# TARGET - this is used by mock so look in /etc/mock for
#          possible options.
#

LOG="echo CTS: "

# required packages
which mock >/dev/null 2>&1
if [ $? -ne 0 ]
then
	$LOG 'please install mock (yum install mock).'
        exit 1
fi

MOCK=/usr/bin/mock

git clean -xfd

set -e

$LOG 'running autogen ...'
./autogen.sh

$LOG 'running configure ...'
./configure --enable-testagents --enable-watchdog --enable-monitoring

$LOG 'building source rpm'
rm -f *.src.rpm
make srpm 
SRPM=$(ls *src.rpm)

if [ ! -f $SRPM ]
then
	$LOG no source rpm to build from!
	exit 1
fi

if [ -z "$TARGET" ]
then
	TARGET=fedora-16-x86_64
fi
case $TARGET in
	fedora-15-x86_64)
	EXTRA_WITH=" --with systemd"
	;;
	fedora-16-x86_64)
	EXTRA_WITH=" --with systemd"
	;;
	fedora-17-x86_64)
	EXTRA_WITH=" --with systemd"
	;;
	*)
esac

RPM_DIR=/var/lib/mock/$TARGET/result
rm -f $RPM_DIR/corosync*.rpm

$LOG "running mock rebuild ($SRPM)"
$MOCK -v -r $TARGET --no-clean --rebuild $SRPM --with testagents --with watchdog --with monitoring $EXTRA_WITH

if [ -z "$TEST_NODES" ]
then
	$LOG no test nodes, exiting without running cts.
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
    *)
    RPM_LIST="$RPM_LIST $r"
    ;;
  esac
done

$LOG installing $RPM_LIST
$LOG onto the test nodes $TEST_NODES

# load and install rpm(s) onto the nodes
for n in $TEST_NODES
do
	$LOG "Installing onto $n"
	sudo ssh $n "rm -rf /tmp/corosync*.rpm"
	sudo ssh $n "rm -f /etc/corosync/corosync.conf.*"
	sudo scp $RPM_LIST $n:/tmp/
        sudo ssh $n "rpm --nodeps --force -Uvf /tmp/corosync*.rpm"
done

$LOG 'running CTS ...'
CTS_LOG=$(pwd)/cts.log
rm -f $CTS_LOG
pushd cts
# needs sudo to read /var/log/messages
	sudo -n ./corolab.py --nodes "$TEST_NODES" --outputfile $CTS_LOG $CTS_ARGS
popd

