#!@BASHPATH@

set -e

msg_count=""
msg_size=""

usage() {
	echo "ploadstart [options]"
	echo ""
	echo "Options:"
	echo " -c msg_count    Number of messages to send (max UINT32_T default 1500000)"
	echo " -s msg_size     Size of messages in bytes  (max 1000000  default 300)"
	echo " -h              display this help"
}

while getopts "hs:c:" optflag; do
		case "$optflag" in
		h)
			usage
			exit 0
		;;
		c)
			msg_count="$OPTARG"
		;;
		s)
			msg_size="$OPTARG"
		;;
		\?|:)
			usage
			exit 1
		;;
		esac
done

[ -n "$msg_count" ] && corosync-cmapctl -s pload.count u32 $msg_count
[ -n "$msg_size" ] && corosync-cmapctl -s pload.size u32 $msg_size

echo "***** WARNING *****"
echo ""
echo "Running pload test will kill your cluster and all corosync daemons will exit"
echo "at the end of the load test"
echo ""
echo "***** END OF WARNING *****"
echo ""
echo "YOU HAVE BEEN WARNED"
echo ""
echo "If you agree, and want to proceed, please type:"
echo "Yes, I fully understand the risks of what I am doing"
echo ""
read -p "type here: " ans

[ "$ans" = "Yes, I fully understand the risks of what I am doing" ] || {
	echo "Wise choice.. or you simply didn't type it right"
	exit 0
}

corosync-cmapctl -s pload.start str i_totally_understand_pload_will_crash_my_cluster_and_kill_corosync_on_exit

echo "PLOAD started, please see corosync.log for final results"
