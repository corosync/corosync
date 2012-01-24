#!/bin/sh

_usage_()
{
  echo bla bla

  exit 0
}

get_mem()
{
  if [ -z "$1" ]
  then
    type=Data
  else
    type=$1
  fi
  MEM=$(cat /proc/$(pidof corosync)/status | grep Vm$type | sed "s/Vm$type:\(.*\) kB/\1/")
  echo $MEM
}

#
# create and destroy a lot of objects
#
_object_test_()
{
  TYPE=RSS
  temp_file=/tmp/object.txt
  COUNT=1

  corosync-cmapctl -s usr.angus u32 456
  corosync-cmapctl -s usr.angus u32 4123
  corosync-cmapctl -d usr.angus

  BEFORE=$(get_mem $TYPE)
  # this loop is just to ignore the first iteration
  for f in /usr/share/man /usr/lib /usr/bin /usr/local ;
  do
    rm -f $temp_file

    find $f | sed "s|\.|_|g" | sed "s|/|.|g" | while read l
    do 
      echo $l.count u64 $COUNT >> $temp_file
      let COUNT="$COUNT+1"
    done

    corosync-cmapctl -p $temp_file
    corosync-cmapctl -D usr
  done
  AFTER=$(get_mem $TYPE)
  let DIFF="$AFTER - $BEFORE"
  rm -f $temp_file
  #echo $f diff $TYPE $DIFF
  echo $DIFF

  exit 0
}

#
# run the corosync tools to cause IPC sessions to created/destroyed
#
_session_test_()
{
  echo _session_test_
  COUNT=1

  corosync-cmap -h >/dev/null
  corosync-cfgtool -h >/dev/null
  corosync-quorumtool -h >/dev/null

  BEFORE=$(get_mem $TYPE)
  corosync-cfgtool -a >/dev/null
  corosync-quorumtool -s >/dev/null
  corosync-quorumtool -l >/dev/null

  find /usr/bin | sed "s|\.|_|g" | sed "s|/|.|g" | while read l
  do 
    corosync-cmapctl -s $l u32 $COUNT
    let COUNT="$COUNT+1"
  done
  corosync-cmapctl -D usr
  AFTER=$(get_mem $TYPE)
  let DIFF="$AFTER - $BEFORE"
  echo $DIFF

  exit 0
}

# Note that we use `"$@"' to let each command-line parameter expand to a 
# separate word. The quotes around `$@' are essential!
# We need TEMP as the `eval set --' would nuke the return value of getopt.
TEMP=`getopt -o u12 --long help,object,session \
     -n '$0' -- "$@"`

if [ $? != 0 ] ; then echo "Incorrect arguments..." >&2 ; _usage_ ; exit 1 ; fi

# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

while true ; do
        case "$1" in
                -u|--help) _usage_ ;;
                -1|--object) _object_test_ ;;
                -2|--session) _session_test_ ;;
                --) shift ; break ;;
                *) echo "Internal error!" ; exit 1 ;;
        esac
done
echo "Remaining arguments:"
for arg do echo '--> '"\`$arg'" ; done




