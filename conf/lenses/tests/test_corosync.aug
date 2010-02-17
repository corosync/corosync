module Test_corosync =

  let conf = "# Please read the corosync.conf.5 manual page
compatibility: whitetank

totem {
	version: 2
	secauth: off
	threads: 0
    clear_node_high_bit: no
    rrp_mode: none
    transport: udp
    token: 1000
	interface {
		ringnumber: 0
		bindnetaddr: 192.168.122.1
		mcastaddr: 226.94.1.1
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
	}
}

amf {
	mode: disabled
}\n"

test Corosync.lns get conf =

  { "#comment" = "Please read the corosync.conf.5 manual page" }
  { "compatibility" = "whitetank" }
  { }
  { "totem"
	{ "version" = "2" }
	{ "secauth" = "off" }
	{ "threads" = "0" }
    { "clear_node_high_bit" = "no" }
    { "rrp_mode" = "none" }
    { "transport" = "udp" }
    { "token" = "1000" }
	{ "interface"
		{ "ringnumber" = "0" }
		{ "bindnetaddr" = "192.168.122.1" }
		{ "mcastaddr" = "226.94.1.1" }
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
		{ "debug" = "on" } } }
  { }
  { "amf"
	{ "mode" = "disabled" } }
