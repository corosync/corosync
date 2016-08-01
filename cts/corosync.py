'''CTS: Cluster Testing System: corosync...
'''

__copyright__='''
Copyright (c) 2010 Red Hat, Inc.
'''

# All rights reserved.
# 
# Author: Angus Salkeld <asalkeld@redhat.com>
#
# This software licensed under BSD license, the text of which follows:
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

import os
import sys
import time
import socket
import shutil
import string

import augeas
from cts.CTS import ClusterManager
from cts.CTSscenarios import ScenarioComponent
from cts.remote import RemoteExec
from cts.remote import RemoteFactory
from cts.logging import *
from cts.CTSvars import CTSvars
from cts.CTSaudits import ClusterAudit
from cts.CTSaudits import LogAudit


###################################################################
class CoroConfig(object):
    def __init__(self, corobase=None):
        self.base = "/files/etc/corosync/corosync.conf/"
        self.new_root = "/tmp/aug-root/"
        if corobase == None: 
            self.corobase = os.getcwd() + "/.."
        else:
            self.corobase = corobase
        example = self.corobase + "/conf/corosync.conf.example"

        if os.path.isdir(self.new_root):
            shutil.rmtree (self.new_root)
        os.makedirs (self.new_root + "/etc/corosync")
        shutil.copy (example, self.new_root + "/etc/corosync/corosync.conf")

        self.aug = augeas.Augeas (root=self.new_root,
                loadpath=self.corobase + "/conf/lenses")

        self.original = {}
        # store the original values (of totem), so we can restore them in
        # apply_default_config()
        totem = self.aug.match('/files/etc/corosync/corosync.conf/totem/*')
        for c in totem:
            # /files/etc/corosync/corosync.conf/
            short_name = c[len(self.base):]
            self.original[short_name] = self.aug.get(c)
        interface = self.aug.match('/files/etc/corosync/corosync.conf/totem/interface/*')
        for c in interface:
            short_name = c[len(self.base):]
            self.original[short_name] = self.aug.get(c)

    def get (self, name):
        return self.aug.get (self.base + name)

    def set (self, name, value):
        token = self.aug.set (self.base + name, str(value))

    def save (self):
        self.aug.save()

    def get_filename(self):
        return self.new_root + "/etc/corosync/corosync.conf"

###################################################################
class corosync_needle(ClusterManager):
    '''
     bla
    '''
    def __init__(self, Environment, randseed=None):
        ClusterManager.__init__(self, Environment, randseed)

        self.update({
            "Name"           : "corosync(needle)",
            "StartCmd"       : "service corosync start",
            "StopCmd"        : "service corosync stop",
            "RereadCmd"      : "service corosync reload",
            "StatusCmd"      : "service corosync status",
            "DeadTime"       : 30,
            "StartTime"      : 15,        # Max time to start up
            "StableTime"     : 10,
            "BreakCommCmd"   : "/usr/share/corosync/tests/net_breaker.sh BreakCommCmd %s",
            "FixCommCmd"     : "/usr/share/corosync/tests/net_breaker.sh FixCommCmd %s",
            "QuorumCmd"      : "corosync-quorumtool -s",

            "Pat:We_stopped"   : "%s.*Corosync Cluster Engine exiting.*",
            "Pat:They_stopped" : "%s.*Member left:.*%s.*",
            "Pat:They_dead"    : "corosync:.*Node %s is now: lost",
            "Pat:Local_starting" : "%s.*Initializing transport",
            "Pat:Local_started" :  "%s.*Initializing transport",
            "Pat:Master_started" : "%s.*Completed service synchronization, ready to provide service.",
            "Pat:Slave_started" :  "%s.*Completed service synchronization, ready to provide service.",
            "Pat:ChildKilled"  : "%s corosync.*Child process %s terminated with signal 9",
            "Pat:ChildRespawn" : "%s corosync.*Respawning failed child process: %s",
            "Pat:ChildExit"    : "Child process .* exited",
            "Pat:DC_IDLE"      : ".*A new membership.*was formed.",
            # Bad news Regexes.  Should never occur.
            "BadRegexes"   : (
                r"ERROR:",
                r"CRIT:",
                r"Shutting down\.",
                r"Forcing shutdown\.",
                r"core dump",
                r"Could not bind AF_UNIX",
                r"Too many open files",
		r"Address already in use",
            ),
            "LogFileName"    : Environment["LogFileName"],
            })
        self.start_cpg = True
        self.cpg_agent = {}
        self.sam_agent = {}
        self.votequorum_agent = {}
        self.config = CoroConfig ()
        self.node_to_ip = {}
        
        self.new_config = {}
        self.applied_config = {}
        for n in self.Env["nodes"]:
            ip = socket.gethostbyname(n)
            ips = ip.split('.')
            ips[3] = '0'
            ip_mask = '.'.join(ips)
            self.new_config['totem/interface/bindnetaddr'] = str(ip_mask)
            return

    def apply_default_config(self):

        for c in self.applied_config:
            if 'bindnetaddr' in c:
                continue
            elif not self.config.original.has_key(c):
                # new config option (non default)
                pass
            elif self.applied_config[c] is not self.config.original[c]:
                # reset to the original
                self.new_config[c] = self.config.original[c]

        if len(self.new_config) > 0:
            self.debug('applying default config')
            self.stopall()

    def apply_new_config(self, need_all_up=True):

        if len(self.new_config) > 0:
            self.debug('applying new config')
            self.stopall()
            if need_all_up:
                self.startall()

    def install_all_config(self):
        tmp1 = {}
        sorted_keys = sorted(self.new_config.keys())
        for c in sorted_keys:
            self.log('configuring: ' + c + ' = '+ str(self.new_config[c]))
            self.config.set (c, self.new_config[c])
            self.applied_config[c] = self.new_config[c]
            tmp1[c] = self.new_config[c]

        for c in tmp1:
            del self.new_config[c]

        self.config.save()
        src_file = self.config.get_filename()
        for node in self.Env["nodes"]:
            self.rsh.cp(src_file, "%s:%s" % (node, "/etc/corosync/"))

    def install_config(self, node):
        # install gets new_config and installs it, then moves the
        # config to applied_config
        if len(self.new_config) > 0:
            self.install_all_config()

    def key_for_node(self, node):
        if not self.node_to_ip.has_key(node):
            self.node_to_ip[node] = socket.gethostbyname (node)
        return self.node_to_ip[node]

    def StartaCM(self, node, verbose=False):

        if not self.ShouldBeStatus.has_key(node):
            self.ShouldBeStatus[node] = "down"

        if self.ShouldBeStatus[node] != "down":
            return 1

        self.debug('starting corosync on : ' + node)
        ret = ClusterManager.StartaCM(self, node)
        if self.start_cpg:
            if self.cpg_agent.has_key(node):
                self.cpg_agent[node].restart()
            else:
                self.cpg_agent[node] = CpgTestAgent(node, self.Env)
                self.cpg_agent[node].start()

        if self.sam_agent.has_key(node):
            self.sam_agent[node].restart()

        # votequorum agent started as needed.
        if self.applied_config.has_key('quorum/provider'):
            if self.applied_config['quorum/provider'] is 'corosync_votequorum':
                if self.votequorum_agent.has_key(node):
                    self.votequorum_agent[node].restart()
                else:
                    self.votequorum_agent[node] = VoteQuorumTestAgent(node, self.Env)
                    self.votequorum_agent[node].start()

        return ret

    def StopaCM(self, node, verbose=False):
        if self.ShouldBeStatus[node] != "up":
            return 1

        self.debug('stoping corosync on : ' + node)
        if self.cpg_agent.has_key(node):
            self.cpg_agent[node].stop()
        if self.sam_agent.has_key(node):
            self.sam_agent[node].stop()
        if self.votequorum_agent.has_key(node):
            self.votequorum_agent[node].stop()
        return ClusterManager.StopaCM(self, node)

    def test_node_CM(self, node):
        # 2 - up and stable
        # 1 - unstable
        # 0 - down
        (rc, lines) = self.rsh(node, self["StatusCmd"], stdout=2)
        out = str(lines)

        if 'systemd' in out:
            if 'running' in out:
                ret = 2
            else:
                ret = 0
        else:
            is_stopped = string.find(out, 'stopped')
            is_dead = string.find(out, 'dead')
            ret = (is_dead is -1 and is_stopped is -1)

        try:
            if ret:
                ret = 2
                if self.ShouldBeStatus[node] == "down":
                    self.log(
                    "Node status for %s is %s but we think it should be %s"
                    %        (node, "up", self.ShouldBeStatus[node]))
            else:
                if self.ShouldBeStatus[node] == "up":
                    self.log(
                    "Node status for %s is %s but we think it should be %s"
                    %        (node, "down", self.ShouldBeStatus[node]))
        except KeyError:        pass

        if ret:        self.ShouldBeStatus[node] = "up"
        else:        self.ShouldBeStatus[node] = "down"
        return ret

    def StataCM(self, node):

        '''Report the status of corosync on a given node'''
        if self.test_node_CM(node) > 0:
            return 1
        else:
            return None

    def RereadCM(self, node):
        self.log('reloading corosync on : ' + node)
        return ClusterManager.RereadCM(self, node)

    def find_partitions(self):
        ccm_partitions = []
        return ccm_partitions

    def prepare(self):
        '''Finish the Initialization process. Prepare to test...'''

        self.partitions_expected = 1
        for node in self.Env["nodes"]:
            self.ShouldBeStatus[node] = ""
            self.unisolate_node(node)
            self.StataCM(node)

    def HasQuorum(self, node_list):
        # If we are auditing a partition, then one side will
        #   have quorum and the other not.
        # So the caller needs to tell us which we are checking
        # If no value for node_list is specified... assume all nodes  
        if not node_list:
            node_list = self.Env["nodes"]

        for node in node_list:
            if self.ShouldBeStatus[node] == "up":
                (quorum, qout) = self.rsh(node, self["QuorumCmd"], stdout=2)
                if quorum == 1:
                    return 1
                elif quorum == 0:
                    return 0
                else:
                    self.log("WARN: Unexpected quorum test result from %s : %d" % (node, quorum))

        return 0

    def Components(self):    
        return None

class ShmLeakAudit(ClusterAudit):

    def __init__(self, cm):
        self.CM = cm

    def name(self):
        return "ShmLeakAudit"

    def is_applicable(self):
        return 1

    def __call__(self):
        rc = 1

        for node in self.CM.Env["nodes"]:
            (res, lines) = self.CM.rsh(node, "/usr/share/corosync/tests/shm_leak_audit.sh", None)
            for line in lines:
                self.CM.log("%s leaked %s" % (node, line))
                rc = 0

        return rc


###################################################################
class TestAgentComponent(ScenarioComponent):
    def __init__(self, Env):
        self.Env = Env

    def IsApplicable(self):
        '''Return TRUE if the current ScenarioComponent is applicable
        in the given LabEnvironment given to the constructor.
        '''
        return True

    def SetUp(self, CM):
        '''Set up the given ScenarioComponent'''
        self.CM = CM
        
        for node in self.Env["nodes"]:
            if not CM.StataCM(node):
                raise RuntimeError ("corosync not up")

            if self.CM.start_cpg:
                if self.CM.cpg_agent.has_key(node):
                    self.CM.cpg_agent[node].clean_start()
                else:
                    self.CM.cpg_agent[node] = CpgTestAgent(node, CM.Env)
                    self.CM.cpg_agent[node].start()
            if self.CM.sam_agent.has_key(node):
                self.CM.sam_agent[node].clean_start()
            else:
                self.CM.sam_agent[node] = SamTestAgent(node, CM.Env)
                self.CM.sam_agent[node].start()
            # votequorum agent started as needed.
            if self.CM.applied_config.has_key('quorum/provider'):
                if CM.applied_config['quorum/provider'] is 'corosync_votequorum':
                    self.CM.votequorum_agent[node] = VoteQuorumTestAgent(node, CM.Env)
                    self.CM.votequorum_agent[node].start()
        return 1

    def TearDown(self, CM):
        '''Tear down (undo) the given ScenarioComponent'''
        self.CM = CM
        for node in self.Env["nodes"]:
            if self.CM.cpg_agent.has_key(node):
                self.CM.cpg_agent[node].stop()
            self.CM.sam_agent[node].stop()
            if self.CM.votequorum_agent.has_key(node):
                self.CM.votequorum_agent[node].stop()

###################################################################
class TestAgent(object):

    def __init__(self, binary, node, port, Env=None):
        self.node = node
        self.node_address = None
        self.port = port
        self.sock = None
        self.binary = binary
        self.started = False
        resh = RemoteFactory.rsh
        self.rsh = RemoteExec(resh)
        self.func_name = None
        self.used = False
        self.env = Env
        self.send_recv = False

    def restart(self):
        LogFactory().debug('%s:%s restarting' % (self.node, self.binary))
        self.stop()
        self.start()

    def clean_start(self):
        if self.used or not self.status():
            LogFactory().debug('%s:%s cleaning' % (self.node, self.binary))
            self.stop()
            self.start()

    def status(self):
        if not self.started:
            return False

        try:
            self.send_internal(["are_you_ok_dude"])
            self.read()
            self.started = True
            return True
        except RuntimeError, msg:
            self.started = False
            return False

    def start(self):
        '''Set up the given ScenarioComponent'''
        if self.status():
            return
        LogFactory().debug('%s:%s starting ' % (self.node, self.binary))

        self.rsh(self.node, self.binary, synchronous=False)
        self.sock = socket.socket (socket.AF_INET, socket.SOCK_STREAM)
        ip = socket.gethostbyname(self.node)
        is_connected = False
        retries = 0
        while not is_connected:
            try:
                retries = retries + 1
                self.sock.connect ((ip, self.port))
                is_connected = True
            except socket.error, msg:
                if retries > 10:
                    LogFactory().log("%s:%s Tried connecting %d times. %s" % (self.node, self.binary, retries, str(msg)))
                if retries > 30:
                    raise RuntimeError("%s:%s can't connect" % (self.node, self.binary))
                time.sleep(1)
        self.started = True
        self.used = False

    def stop(self):
        '''Tear down (undo) the given ScenarioComponent'''
        LogFactory().debug('%s:%s stopping' % (self.binary, self.node))
        self.rsh(self.node, "killall " + self.binary + " 2>/dev/null")
        if self.sock:
            self.sock.close ()
            del self.sock
            self.sock = None
        while self.getpid() != '':
            time.sleep(1)
        self.started = False

    def kill(self):
        '''Tear down (undo) the given ScenarioComponent'''
        LogFactory().log('%s:%s killing' % (self.node, self.binary))
        self.rsh(self.node, "killall -9 " + self.binary + " 2>/dev/null")
        self.started = False

    def getpid(self):
        return self.rsh(self.node, 'pidof ' + self.binary, 1)

    def send_internal(self, args):

        real_msg = str (len (args))
        for a in args:
            a_str = str(a)
            real_msg += ":" + str (len (a_str)) + ":" + a_str
        real_msg += ";"
        try:
            return self.sock.send (real_msg)
        except socket.error, msg:
            LogFactory().log("send(%s): %s; error: %s" % (self.node, real_msg, msg))
            return 0

    def send (self, args):
        if not self.started:
            self.start()

        sent = self.send_internal(args)

        if sent == 0:
            raise RuntimeError ("socket connection broken")
        self.used = True

    def __getattribute__(self,name):

        try:
            return object.__getattribute__(self, name)
        except:
            self.func_name = name
            if self.send_recv:
                return self.send_recv_dynamic
            else:
                return self.send_dynamic

    def send_recv_dynamic (self, *args):
        self.send_dynamic (args)

        try:
            res = self.read ()
        except RuntimeError, msg:
            res = None
            LogFactory().log("send_recv_dynamic: %s(); error: %s" % (self.func_name, msg))

        return res


    def send_dynamic (self, *args):
        if not self.started:
            raise RuntimeError ("agent not started")

        # number of args+func
        real_msg = str (len (args) + 1) + ":" + str(len(self.func_name)) + ":" + self.func_name
        for a in args:
            a_str = str(a)
            real_msg += ":" + str (len (a_str)) + ":" + a_str
        real_msg += ";"
        sent = 0
        try:
            sent = self.sock.send (real_msg)
        except socket.error, msg:
            LogFactory().log("send_dynamic(%s): %s; error: %s" % (self.node, real_msg, msg))

        if sent == 0:
            raise RuntimeError ("socket connection broken")
        self.used = True

    def read (self):

        try:
            msg = self.sock.recv (4096)
        except socket.error, msg:
            raise RuntimeError(msg)

        if msg == '':
            raise RuntimeError("socket connection broken")
        return msg


class CpgConfigEvent:
    def __init__(self, msg):
        info = msg.split(',')
        self.group_name = info[0]
        self.node_id = info[1]
        self.node = None
        self.pid = info[2]
        if "left" in info[3]:
            self.is_member = False
        else:
            self.is_member = True

    def __str__ (self):

        str = self.group_name + "," + self.node_id + "," + self.pid + ","
        if self.is_member:
            return str + "joined"
        else:
            return str + "left"

###################################################################
class CpgTestAgent(TestAgent):

    def __init__(self, node, Env=None):
        TestAgent.__init__(self, "cpg_test_agent", node, 9034, Env)
        self.nodeid = None

    def start(self):
        if not self.status():
            TestAgent.start(self)
            self.cpg_initialize()
            self.used = False

    def cpg_local_get(self):
        if self.nodeid == None:
            self.send (["cpg_local_get"])
            self.nodeid = self.read ()
        return self.nodeid

    def record_config_events(self, truncate=True):
        if truncate:
            self.send (["record_config_events", "truncate"])
        else:
            self.send (["record_config_events", "append"])
        return self.read ()

    def read_config_event(self):
        self.send (["read_config_event"])
        msg = self.read ()

        if "None" in msg:
            return None
        else:
            return CpgConfigEvent(msg)

    def read_messages(self, atmost):
        self.send (["read_messages", atmost])
        msg = self.read ()

        if "None" in msg:
            return None
        else:
            return msg

    def context_test(self):
        self.send (["context_test"])
        return self.read ()

###################################################################
class SamTestAgent(TestAgent):

    def __init__(self, node, Env=None):
        TestAgent.__init__(self, "sam_test_agent", node, 9036, Env)
        self.nodeid = None
        self.send_recv = True

###################################################################
class VoteQuorumTestAgent(TestAgent):

    def __init__(self, node, Env=None):
        TestAgent.__init__(self, "votequorum_test_agent", node, 9037, Env)
        self.nodeid = None
        self.send_recv = True

AllAuditClasses = []
AllAuditClasses.append(LogAudit)
AllAuditClasses.append(ShmLeakAudit)

def CoroAuditList(cm):
    result = []
    for auditclass in AllAuditClasses:
        a = auditclass(cm)
        if a.is_applicable():
            result.append(a)
    return result



