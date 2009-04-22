#!/bin/sh
#
# author: Angus Salkeld (ahsalkeld@gmail.com)
#
# usage:
#  run this from the base directory of corosync
#


SRCDIR=$(pwd)
ALL_TESTS="1 2 3 4"

MAKE_LOG=/tmp/corosync-make-test.log

test_1()
{
	TEST="[1] simple make"
	rm -f $SRCDIR/make_o_path
	make >$MAKE_LOG 2>&1
	return $?
}

make_clean()
{
	if [ -f $SRCDIR/make_o_path ]
	then
		make $(cat $SRCDIR/make_o_path) clean >$MAKE_LOG 2>&1
		RES=$?
	else
		if [ -n "$BUILD_DIR" ]
		then
			pushd $BUILD_DIR >/dev/null
			make -f $SRCDIR/Makefile clean >$MAKE_LOG 2>&1
			RES=$?
			popd >/dev/null
		else
			make clean >$MAKE_LOG 2>&1
			RES=$?
		fi

	fi
	return $RES
}

test_2()
{
	rm -f $SRCDIR/make_o_path
	TEST="[2] make from exec dir"
	pushd $SRCDIR/exec >/dev/null
	make >$MAKE_LOG 2>&1
	RES=$?
	popd >/dev/null
	return $RES
}

test_3()
{
	local BUILD_DIR=/tmp/corosync-make-test
	echo "O=$BUILD_DIR" > $SRCDIR/make_o_path

	TEST="[3] make objects separately from the source"
	rm -rf $BUILD_DIR
	make O=$BUILD_DIR >$MAKE_LOG 2>&1
	unset BUILD_DIR
	return $?
}

test_4()
{
	BUILD_DIR=/tmp/corosync-make-test
	rm -f $SRCDIR/make_o_path

	TEST="[4] make -f SRCDIR/Makefile from the builddir"

	rm -rf $BUILD_DIR
	mkdir -p $BUILD_DIR

	pushd $BUILD_DIR >/dev/null
	make -f $SRCDIR/Makefile >$MAKE_LOG 2>&1
	RES=$?
	popd >/dev/null
	return $RES
}

if [ -n "$1" ]
then
	TESTS_TO_RUN=$1
else
	TESTS_TO_RUN=$ALL_TESTS
fi

for t in $TESTS_TO_RUN
do
	test_$t
	if [ $? -ne 0 ]
	then
		echo "$0 $TEST [failed]."
		cat $MAKE_LOG
		exit 1
	else
		echo "$0 $TEST [passed]."
	fi
	make_clean
	if [ $? -ne 0 ]
	then
		echo "$0 $TEST [failed to clean]."
		cat $MAKE_LOG
		exit 1
	else
		echo "$0 $TEST [cleaned]."
	fi
done

echo $0 all make tests passed!
exit 0
