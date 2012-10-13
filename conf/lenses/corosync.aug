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

(* A integer subsection *)
let interface =
  let setting =
    kv "ringnumber" Rx.integer
    |kv "mcastport" Rx.integer
    |kv "ttl" Rx.integer
    |qstr /bindnetaddr|mcastaddr/ in
  section "interface" setting

(* The totem section *)
let totem =
  let setting =
    kv "clear_node_high_bit" /yes|no/
    |kv "rrp_mode" /none|active|passive/
    |kv "vsftype" /none|ykd/
    |kv "secauth" /on|off/
    |kv "crypto_type" /nss|aes256|aes192|aes128|3des/
    |kv "crypto_cipher" /none|nss|aes256|aes192|aes128|3des/
    |kv "crypto_hash" /none|md5|sha1|sha256|sha384|sha512/
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
   |kv "debug" /yes|no|on|off|trace/
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


(* The resource section *)
let common_resource =
   kv "max" Rx.decimal
   |kv "poll_period" Rx.integer
   |kv "recovery" /reboot|shutdown|watchdog|none/

let memory_used =
    let setting =
    common_resource in
  section "memory_used" setting


let load_15min =
    let setting =
    common_resource in
  section "load_15min" setting

let system =
    let setting =
     load_15min
     |memory_used in
   section "system" setting

(* The resources section *)
let resources =
  let setting =
    system in
  section "resources" setting

(* The quorum section *)
let quorum =
  let setting =
   qstr /provider/
   |kv "expected_votes" Rx.integer
   |kv "votes" Rx.integer
   |kv "wait_for_all" Rx.integer
   |kv "last_man_standing" Rx.integer
   |kv "last_man_standing_window" Rx.integer
   |kv "auto_tie_breaker" Rx.integer
   |kv "two_node" Rx.integer in
  section "quorum" setting

(* The service section *)
let service =
  let setting =
   qstr /name|ver/ in
  section "service" setting

(* The uidgid section *)
let uidgid =
  let setting =
   qstr /uid|gid/ in
  section "uidgid" setting

(* The node section *)
let node =
  let setting =
   qstr /ring[0-9]_addr/
   |kv "nodeid" Rx.integer
   |kv "quorum_votes" Rx.integer in
  section "node" setting

(* The nodelist section *)
let nodelist =
  let setting =
    node in
  section "nodelist" setting

let lns = (comment|empty|totem|quorum|logging|resources|service|uidgid|nodelist)*

let xfm = transform lns (incl "/etc/corosync/corosync.conf")
