module Test_corosync =

  let conf = "# Please read the corosync.conf.5 manual page

totem {
	version: 2
	secauth: off
	crypto_cipher: none
	crypto_hash: none
	threads: 0
	clear_node_high_bit: no
	rrp_mode: none
	transport: udp
	token: 1000
	interface {
		ringnumber: 0
		bindnetaddr: 192.168.122.1
		mcastaddr: 226.94.1.1
		ttl: 45
		mcastport: 5405
	}
}

logging {
	fileline: off
    function_name: on
	to_stderr: yes
	to_logfile: yes
	to_syslog: yes
	logfile: /tmp/corosync.log
	debug: off
	timestamp: on
	logger_subsys {
	    to_syslog: no
		subsys: CPG
		debug: on
	}
	logger_subsys {
	    to_stderr: no
	    logfile: /tmp/corosync-msg.log
		subsys: MSG
		debug: on
		tags: enter|trace4
	}
}

quorum {
    provider: corosync_votequorum
    expected_votes: 5
    votes: 2
    two_node: 1
    wait_for_all: 1
    last_man_standing: 1
    last_man_standing_window: 10000
    auto_tie_breaker: 1
}

resources {
	system {
		memory_used {
			recovery: reboot
			max: 80
		}
		load_15min {
			recovery: watchdog
			max: 8.56
		}
	}
}

uidgid {
    uid: 0
    gid: 0
}

nodelist {
	node {
		ring0_addr: 192.168.122.1
		nodeid: 1
		quorum_votes: 2
	}

	node {
		ring0_addr: 192.168.122.2
		ring1_addr: 192.168.123.1
		nodeid: 2
	}
}\n"

test Corosync.lns get conf =

  { "#comment" = "Please read the corosync.conf.5 manual page" }
  { }
  { "totem"
	{ "version" = "2" }
	{ "secauth" = "off" }
	{ "crypto_cipher" = "none" }
	{ "crypto_hash" = "none" }
	{ "threads" = "0" }
    { "clear_node_high_bit" = "no" }
    { "rrp_mode" = "none" }
    { "transport" = "udp" }
    { "token" = "1000" }
	{ "interface"
		{ "ringnumber" = "0" }
		{ "bindnetaddr" = "192.168.122.1" }
		{ "mcastaddr" = "226.94.1.1" }
		{ "ttl" = "45" }
		{ "mcastport" = "5405" } } }
  { }
  { "logging"
	{ "fileline" = "off" }
	{ "function_name" = "on" }
	{ "to_stderr" = "yes" }
	{ "to_logfile" = "yes" }
	{ "to_syslog" = "yes" }
	{ "logfile" = "/tmp/corosync.log" }
	{ "debug" = "off" }
	{ "timestamp" = "on" }
	{ "logger_subsys"
	    { "to_syslog" = "no" }
		{ "subsys" = "CPG" }
		{ "debug" = "on" } }
	{ "logger_subsys"
	    { "to_stderr" = "no" }
	    { "logfile" = "/tmp/corosync-msg.log" }
		{ "subsys" = "MSG" }
		{ "debug" = "on" }
		{ "tags" = "enter|trace4" } } }
  { }
  { "quorum"
    { "provider" = "corosync_votequorum" }
    { "expected_votes" = "5" }
    { "votes" = "2" }
    { "two_node" = "1" }
    { "wait_for_all" = "1" }
    { "last_man_standing" = "1" }
    { "last_man_standing_window" = "10000" }
    { "auto_tie_breaker" = "1" } }
  { }
    { "resources"
	  { "system"
		{ "memory_used"
			{ "recovery" = "reboot" }
			{ "max" = "80" } }
		{ "load_15min"
			{ "recovery" = "watchdog" }
			{ "max" = "8.56" } } } }
  { }
  { "uidgid"
    { "uid" = "0" }
    { "gid" = "0" } }
  { }
  { "nodelist"
    { "node"
      { "ring0_addr" = "192.168.122.1" }
      { "nodeid" = "1" }
      { "quorum_votes" = "2" } }
    { }
    { "node"
      { "ring0_addr" = "192.168.122.2" }
      { "ring1_addr" = "192.168.123.1" }
      { "nodeid" = "2" } } }
