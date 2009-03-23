#!/bin/sh
# Run this to generate all the initial makefiles, etc.

testProgram()
{
  cmd=$1

  if [ -z "$cmd" ]; then
    return 1;
  fi

  arch=`uname -s`

  # Make sure the which is in an if-block... on some platforms it throws exceptions
  #
  # The ERR trap is not executed if the failed command is part
  #   of an until or while loop, part of an if statement, part of a &&
  #   or  ||  list.
  if
     which $cmd  </dev/null >/dev/null 2>&1
  then
      :
  else
      return 1
  fi

  # The GNU standard is --version
  if 
      $cmd --version </dev/null >/dev/null 2>&1
  then
      return 0 
  fi

  # Maybe it suppports -V instead
  if 
      $cmd -V </dev/null >/dev/null 2>&1
  then
      return 0 
  fi

  # Nope, the program seems broken
  return 1
}

arch=`uname -s`
# Disable the errors on FreeBSD until a fix can be found.
if [ ! "$arch" = "FreeBSD" ]; then
set -e
#
#	All errors are fatal from here on out...
#	The shell will complain and exit on any "uncaught" error code.
#
#
#	And the trap will ensure sure some kind of error message comes out.
#
trap 'echo ""; echo "$0 exiting due to error (sorry!)." >&2' 0
fi

RC=0

gnu="ftp://ftp.gnu.org/pub/gnu"

# Check for Autoconf
for command in autoconf autoconf213 autoconf253 autoconf259 
do
  if
      testProgram $command == 1
  then
    autoconf=$command
    autoheader=`echo  "$autoconf" | sed -e 's/autoconf/autoheader/'`
    autom4te=`echo  "$autoconf" | sed -e 's/autoconf/autmo4te/'`
    autoreconf=`echo  "$autoconf" | sed -e 's/autoconf/autoreconf/'`
    autoscan=`echo  "$autoconf" | sed -e 's/autoconf/autoscan/'`
    autoupdate=`echo  "$autoconf" | sed -e 's/autoconf/autoupdate/'`
    ifnames=`echo  "$autoconf" | sed -e 's/autoconf/ifnames/'`
  fi
done

# Check for automake
for command in automake19 automake-1.9 automake
do
  if 
      testProgram $command
  then
    automake=$command
    aclocal=`echo  "$automake" | sed -e 's/automake/aclocal/'`

  fi
done

if [ -z $autoconf ]; then 
    echo You must have autoconf installed to compile the corosync package.
    echo Download the appropriate package for your system,
    echo or get the source tarball at: $gnu/autoconf/
    exit 1

elif [ -z $automake ]; then 
    echo You must have automake installed to compile the corosync package.
    echo Download the appropriate package for your system,
    echo or get the source tarball at: $gnu/automake/
    exit 1
fi

# Create local copies so that the incremental updates will work.
rm -f ./autoconf ./automake ./autoheader
ln -s `which $autoconf` ./autoconf
ln -s `which $automake` ./automake
ln -s `which $autoheader` ./autoheader

printf "$autoconf:\t"
$autoconf --version | head -n 1 

printf "$automake:\t"
$automake --version | head -n 1

echo $aclocal $ACLOCAL_FLAGS
$aclocal $ACLOCAL_FLAGS

echo $autoheader
$autoheader

echo $automake --add-missing --include-deps --copy
$automake --add-missing --include-deps --copy

echo $autoconf
$autoconf

echo Now run ./configure
trap '' 0
