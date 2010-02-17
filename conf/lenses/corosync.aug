(* Process /etc/corosync/corosync.conf                             *)
(* The lens is based on the corosync.conf(5) man page     *)
module Corosync =

autoload xfm

let comment = Util.comment
let empty = Util.empty
let dels = Util.del_str
let eol = Util.eol

let ws = del /[ \t]+/ " "
let wsc = del /:[ \t]+/ ": "
let indent = del /[ \t]*/ ""
(* We require that braces are always followed by a newline *)
let obr = del /\{([ \t]*)\n/ "{\n"
let cbr = del /[ \t]*}[ \t]*\n/ "}\n"

let ikey (k:regexp) = indent . key k

let section (n:regexp) (b:lens) =
  [ ikey n . ws . obr . (b|empty|comment)* . cbr ]

let kv (k:regexp) (v:regexp) =
  [ ikey k .  wsc . store v . eol ]

(* FIXME: it would be much more concise to write                       *)
(* [ key k . ws . (bare | quoted) ]                                    *)
(* but the typechecker trips over that                                 *)
let qstr (k:regexp) =
  let delq = del /['"]/ "\"" in
  let bare = del /["']?/ "" . store /[^"' \t\n]+/ . del /["']?/ "" in
  let quoted = delq . store /.*[ \t].*/ . delq in
  [ ikey k . wsc . bare . eol ]
 |[ ikey k . wsc . quoted . eol ]

(* The compatibility option *)
let compatibility = kv "compatibility" /whitetank|none/


(* A integer subsection *)
let interface =
  let setting =
    kv "ringnumber" Rx.integer
    |kv "mcastport" Rx.integer
    |qstr /bindnetaddr|mcastaddr/ in
  section "interface" setting

(* The totem section *)
let totem =
  let setting =
    kv "clear_node_high_bit" /yes|no/
    |kv "rrp_mode" /none|active|passive/
    |kv "vsftype" /none|ykd/
    |kv "secauth" /on|off/
    |kv "transport" /udp|iba/
    |kv "version" Rx.integer
    |kv "nodeid" Rx.integer
    |kv "threads" Rx.integer
    |kv "netmtu" Rx.integer
    |kv "token" Rx.integer
    |kv "token_retransmit" Rx.integer
    |kv "hold" Rx.integer
    |kv "token_retransmits_before_loss_const" Rx.integer
    |kv "join" Rx.integer
    |kv "send_join" Rx.integer
    |kv "consensus" Rx.integer
    |kv "merge" Rx.integer
    |kv "downcheck" Rx.integer
    |kv "fail_to_recv_const" Rx.integer
    |kv "seqno_unchanged_const" Rx.integer
    |kv "heartbeat_failures_allowed" Rx.integer
    |kv "max_network_delay" Rx.integer
    |kv "max_messages" Rx.integer
    |kv "window_size" Rx.integer
    |kv "rrp_problem_count_timeout" Rx.integer
    |kv "rrp_problem_count_threshold" Rx.integer
    |kv "rrp_token_expired_timeout" Rx.integer
    |interface in
  section "totem" setting

let common_logging =
   kv "to_syslog" /yes|no|on|off/
   |kv "to_stderr" /yes|no|on|off/
   |kv "to_logfile" /yes|no|on|off/
   |kv "debug" /yes|no|on|off/
   |kv "logfile_priority" /alert|crit|debug|emerg|err|info|notice|warning/
   |kv "syslog_priority" /alert|crit|debug|emerg|err|info|notice|warning/
   |kv "syslog_facility" /daemon|local0|local1|local2|local3|local4|local5|local6|local7/
   |qstr /logfile|tags/

(* A logger_subsys subsection *)
let logger_subsys =
  let setting =
    qstr /subsys/
   |common_logging in
  section "logger_subsys" setting


(* The logging section *)
let logging =
  let setting =
   kv "fileline" /yes|no|on|off/
   |kv "function_name" /yes|no|on|off/
   |kv "timestamp" /yes|no|on|off/
   |common_logging
   |logger_subsys in
  section "logging" setting


(* The amf section *)
let amf =
  let setting =
   kv "mode" /enabled|disabled/ in
  section "amf" setting


let lns = (comment|empty|compatibility|totem|logging|amf)*

let xfm = transform lns (incl "/etc/corosync/corosync.conf")
