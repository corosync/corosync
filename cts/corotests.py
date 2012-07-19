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

import random
import socket
from UserDict import UserDict
from cts.CTStests import *
from corosync import CpgTestAgent

###################################################################
class CoroTest(CTSTest):
    '''
    basic class to make sure that new configuration is applied
    and old configuration is removed.
    '''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.start = StartTest(cm)
        self.stop = StopTest(cm)
        self.config = {}
        self.config['logging/logger_subsys[1]/subsys'] = 'MAIN'
        self.config['logging/logger_subsys[1]/debug'] = 'on'
        self.need_all_up = True
        self.CM.start_cpg = True
        self.cpg_name = 'cts_group'

    def setup(self, node):
        ret = CTSTest.setup(self, node)

        # setup the authkey
        localauthkey = '/tmp/authkey'
        if not os.path.exists(localauthkey):
            self.CM.rsh(node, 'corosync-keygen -l')
            self.CM.rsh.cp("%s:%s" % (node, "/etc/corosync/authkey"), localauthkey)

        for n in self.CM.Env["nodes"]:
            if n is not node:
                #copy key onto other nodes
                self.CM.rsh.cp(localauthkey, "%s:%s" % (n, "/etc/corosync/authkey"))

        # copy over any new config
        for c in self.config:
            self.CM.new_config[c] = self.config[c]

        # apply the config
        self.CM.apply_new_config(self.need_all_up)

        # start/stop all corosyncs'
        for n in self.CM.Env["nodes"]:
            if self.need_all_up and not self.CM.StataCM(n):
                self.incr("started")
                self.start(n)
            if self.need_all_up and self.CM.start_cpg:
                self.CM.cpg_agent[n].clean_start()
                self.CM.cpg_agent[n].cpg_join(self.cpg_name)
                self.CM.cpg_agent[n].cfg_initialize()
            if not self.need_all_up and self.CM.StataCM(n):
                self.incr("stopped")
                self.stop(n)
        return ret

    def config_valid(self, config):
        return True

    def teardown(self, node):
        self.CM.apply_default_config()
        return CTSTest.teardown(self, node)

###################################################################
class CpgContextTest(CoroTest):
    def __init__(self, cm):
        self.name="CpgContextTest"
        CoroTest.__init__(self, cm)
        self.CM.start_cpg = True

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.cpg_agent[node].context_test()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('context_test failed')

###################################################################
class CpgConfigChangeBase(CoroTest):
    '''
    join a cpg group on each node, and test that the following
    causes a leave event:
    - a call to cpg_leave()
    - app exit
    - node leave
    - node leave (with large token timeout)
    '''

    def setup(self, node):
        ret = CoroTest.setup(self, node)

        self.listener = None
        self.wobbly = None
        for n in self.CM.Env["nodes"]:
            if self.wobbly is None:
                self.wobbly = n
            elif self.listener is None:
                self.listener = n

        if self.CM.cpg_agent.has_key(self.wobbly):
            self.wobbly_id = self.CM.cpg_agent[self.wobbly].cpg_local_get()
        if self.CM.cpg_agent.has_key(self.listener):
            self.CM.cpg_agent[self.listener].record_config_events(truncate=True)

        return ret

    def wait_for_config_change(self):
        found = False
        max_timeout = 60 * 15
        waited = 0
        printit = 0
        self.CM.log("Waiting for config change on " + self.listener)
        while not found:
            try:
                event = self.CM.cpg_agent[self.listener].read_config_event()
            except:
                return self.failure('connection to test cpg_agent failed.')
            if not event == None:
                self.CM.debug("RECEIVED: " + str(event))
            if event == None:
                if waited >= max_timeout:
                    return self.failure("timedout(" + str(waited) + " sec) == no event!")
                else:
                    time.sleep(1)
                    waited = waited + 1
                    printit = printit + 1
                    if printit is 60:
                        print 'waited ' + str(waited) + ' seconds'
                        printit = 0

            elif str(event.node_id) in str(self.wobbly_id) and not event.is_member:
                self.CM.log("Got the config change in " + str(waited) + " seconds")
                found = True
            else:
                self.CM.debug("No match")
                self.CM.debug("wobbly nodeid:" + str(self.wobbly_id))
                self.CM.debug("event nodeid:" + str(event.node_id))
                self.CM.debug("event.is_member:" + str(event.is_member))

        if found:
            return self.success()


###################################################################
class CpgCfgChgOnGroupLeave(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnGroupLeave"

    def failure_action(self):
        self.CM.log("calling cpg_leave() on " + self.wobbly)
        self.CM.cpg_agent[self.wobbly].cpg_leave(self.cpg_name)

    def __call__(self, node):
        self.incr("calls")
        self.failure_action()
        return self.wait_for_config_change()

###################################################################
class CpgCfgChgOnNodeLeave(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnNodeLeave"

    def failure_action(self):
        self.CM.log("stopping corosync on " + self.wobbly)
        self.stop(self.wobbly)

    def __call__(self, node):
        self.incr("calls")
        self.failure_action()
        return self.wait_for_config_change()

###################################################################
class CpgCfgChgOnLowestNodeJoin(CTSTest):
    '''
    1) stop all nodes
    2) start all but the node with the smallest ip address
    3) start recording events
    4) start the last node
    '''
    def __init__(self, cm):
        CTSTest.__init__(self, cm)
        self.name="CpgCfgChgOnLowestNodeJoin"
        self.start = StartTest(cm)
        self.stop = StopTest(cm)
        self.config = {}
        self.need_all_up = False

    def config_valid(self, config):
        return True

    def lowest_ip_set(self):
        self.lowest = None
        for n in self.CM.Env["nodes"]:
            if self.lowest is None:
                self.lowest = n

        self.CM.log("lowest node is " + self.lowest)

    def setup(self, node):
        # stop all nodes
        for n in self.CM.Env["nodes"]:
            self.CM.StopaCM(n)

        self.lowest_ip_set()

        # copy over any new config
        for c in self.config:
            self.CM.new_config[c] = self.config[c]

        # install the config
        self.CM.install_all_config()

        # start all but lowest
        self.listener = None
        for n in self.CM.Env["nodes"]:
            if n is not self.lowest:
                if self.listener is None:
                    self.listener = n
                self.incr("started")
                self.CM.log("starting " + n)
                self.start(n)
                self.CM.cpg_agent[n].clean_start()
                self.CM.cpg_agent[n].cpg_join(self.cpg_name)

        # start recording events
        pats = []
        pats.append("%s .*sync: node joined.*" % self.listener)
        pats.append("%s .*sync: activate correctly.*" % self.listener)
        self.sync_log = self.create_watch(pats, 60)
        self.sync_log.setwatch()

        self.CM.log("setup done")

        return CTSTest.setup(self, node)


    def __call__(self, node):
        self.incr("calls")

        self.start(self.lowest)
        self.CM.cpg_agent[self.lowest].clean_start()
        self.CM.cpg_agent[self.lowest].cpg_join(self.cpg_name)
        self.wobbly_id = self.CM.cpg_agent[self.lowest].cpg_local_get()

        self.CM.log("waiting for sync events")
        if not self.sync_log.lookforall():
            return self.failure("Patterns not found: " + repr(self.sync_log.unmatched))
        else:
            return self.success()


###################################################################
class CpgCfgChgOnExecCrash(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnExecCrash"

    def failure_action(self):
        self.CM.log("sending KILL to corosync on " + self.wobbly)
        self.CM.rsh(self.wobbly, "killall -9 corosync")
        self.CM.rsh(self.wobbly, "rm -f /var/run/corosync.pid")
        self.CM.rsh(self.wobbly, "rm -f /dev/shm/qb-corosync-blackbox*")
        self.CM.ShouldBeStatus[self.wobbly] = "down"

    def __call__(self, node):
        self.incr("calls")
        self.failure_action()
        return self.wait_for_config_change()


###################################################################
class CpgCfgChgOnNodeIsolate(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnNodeIsolate"

    def config_valid(self, config):
        if config.has_key('totem/rrp_mode'):
            return False
        else:
            return True

    def failure_action(self):
        self.CM.log("isolating node " + self.wobbly)
        self.CM.isolate_node(self.wobbly)

    def __call__(self, node):
        self.incr("calls")
        self.failure_action()
        return self.wait_for_config_change()

    def teardown(self, node):
        self.CM.unisolate_node (self.wobbly)
        return CpgConfigChangeBase.teardown(self, node)

###################################################################
class CpgCfgChgOnNodeRestart(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnNodeRestart"
        self.CM.start_cpg = False

    def config_valid(self, config):
        if config.has_key('totem/secauth'):
            if config['totem/secauth'] is 'on':
                return False
            else:
                return True
        if config.has_key('totem/rrp_mode'):
            return False
        else:
            return True

    def failure_action(self):
        self.CM.log("2: isolating node " + self.wobbly)
        self.CM.isolate_node(self.wobbly)
        self.CM.log("3: Killing corosync on " + self.wobbly)
        self.CM.rsh(self.wobbly, "killall -9 corosync")
        self.CM.rsh(self.wobbly, "rm -f /var/run/corosync.pid")
        self.CM.ShouldBeStatus[self.wobbly] = "down"
        self.CM.log("4: unisolating node " + self.wobbly)
        self.CM.unisolate_node (self.wobbly)
        self.CM.log("5: starting corosync on " + self.wobbly)
        self.CM.StartaCM(self.wobbly)
        time.sleep(5)
        self.CM.log("6: starting cpg on all nodes")
        self.CM.start_cpg = True
        for node in self.CM.Env["nodes"]:
            self.CM.cpg_agent[node] = CpgTestAgent(node, self.CM.Env)
            self.CM.cpg_agent[node].start()
            self.CM.cpg_agent[node].cpg_join(self.cpg_name)

        self.wobbly_id = self.CM.cpg_agent[self.wobbly].cpg_local_get()
        self.CM.cpg_agent[self.listener].record_config_events(truncate=True)

        self.CM.log("7: isolating node " + self.wobbly)
        self.CM.isolate_node(self.wobbly)
        self.CM.log("8: Killing corosync on " + self.wobbly)
        self.CM.rsh(self.wobbly, "killall -9 corosync")
        self.CM.rsh(self.wobbly, "rm -f /var/run/corosync.pid")
        self.CM.ShouldBeStatus[self.wobbly] = "down"
        self.CM.log("9: unisolating node " + self.wobbly)
        self.CM.unisolate_node (self.wobbly)
        self.CM.log("10: starting corosync on " + self.wobbly)
        self.CM.StartaCM(self.wobbly)

    def __call__(self, node):
        self.incr("calls")
        self.failure_action()
        return self.wait_for_config_change()

    def teardown(self, node):
        self.CM.unisolate_node (self.wobbly)
        return CpgConfigChangeBase.teardown(self, node)

###################################################################
class CpgMsgOrderBase(CoroTest):

    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.num_msgs_per_node = 0
        self.total_num_msgs = 0

    def setup(self, node):
        ret = CoroTest.setup(self, node)

        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].clean_start()
            self.CM.cpg_agent[n].cpg_join(self.cpg_name)
            self.CM.cpg_agent[n].record_messages()

        time.sleep(1)
        return ret

    def cpg_msg_blaster(self):
        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].msg_blaster(self.num_msgs_per_node)

    def wait_and_validate_order(self):
        msgs = {}

        self.total_num_msgs = 0
        for n in self.CM.Env["nodes"]:
            self.total_num_msgs = self.total_num_msgs + self.num_msgs_per_node

        for n in self.CM.Env["nodes"]:
            msgs[n] = []
            stopped = False
            waited = 0

            while len(msgs[n]) < self.total_num_msgs and waited < 360:

                try:
                    msg = self.CM.cpg_agent[n].read_messages(50)
                except:
                    return self.failure('connection to test cpg_agent failed.')

                if not msg == None:
                    msgl = msg.split(";")

                    # remove empty entries
                    not_done=True
                    while not_done:
                        try:
                            msgl.remove('')
                        except:
                            not_done = False

                    msgs[n].extend(msgl)
                elif msg == None:
                    time.sleep(2)
                    waited = waited + 2

            if len(msgs[n]) < self.total_num_msgs:
                return self.failure("expected %d messages from %s got %d" % (self.total_num_msgs, n, len(msgs[n])))

        fail = False
        error_message = ''
        for i in range(0, self.total_num_msgs):
            first = None
            for n in self.CM.Env["nodes"]:
                # first test for errors
                params = msgs[n][i].split(":")
                if not 'OK' in params[3]:
                    fail = True
                    error_message = 'error: ' + params[3] + ' in received message'
                    self.CM.log(str(params))

                # then look for out of order messages
                if first == None:
                    first = n
                else:
                    if not msgs[first][i] == msgs[n][i]:
                        # message order not the same!
                        fail = True
                        error_message = 'message out of order'
                        self.CM.log(msgs[first][i] + " != " + msgs[n][i])

        if fail:
            return self.failure(error_message)
        else:
            return self.success()

###################################################################
class CpgMsgOrderBasic(CpgMsgOrderBase):
    '''
    each sends & logs lots of messages
    '''
    def __init__(self, cm):
        CpgMsgOrderBase.__init__(self,cm)
        self.name="CpgMsgOrderBasic"
        self.num_msgs_per_node = 9000

    def __call__(self, node):
        self.incr("calls")
        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].msg_blaster(self.num_msgs_per_node)

        return self.wait_and_validate_order()

###################################################################
class CpgMsgOrderZcb(CpgMsgOrderBase):
    '''
    each sends & logs lots of messages
    '''
    def __init__(self, cm):
        CpgMsgOrderBase.__init__(self,cm)
        self.name="CpgMsgOrderZcb"
        self.num_msgs_per_node = 9000

    def __call__(self, node):
        self.incr("calls")
        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].msg_blaster_zcb(self.num_msgs_per_node)
        return self.wait_and_validate_order()

###################################################################
class MemLeakObject(CoroTest):
    '''
    run mem_leak_test.sh -1
    '''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="MemLeakObject"

    def __call__(self, node):
        self.incr("calls")

        mem_leaked = self.CM.rsh(node, "/usr/share/corosync/tests/mem_leak_test.sh -1")
        if mem_leaked is 0:
            return self.success()
        else:
            return self.failure(str(mem_leaked) + 'kB memory leaked.')

###################################################################
class MemLeakSession(CoroTest):
    '''
    run mem_leak_test.sh -2
    '''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="MemLeakSession"

    def __call__(self, node):
        self.incr("calls")

        mem_leaked = self.CM.rsh(node, "/usr/share/corosync/tests/mem_leak_test.sh -2")
        if mem_leaked is 0:
            return self.success()
        else:
            return self.failure(str(mem_leaked) + 'kB memory leaked.')

###################################################################
class CMapDispatchDeadlock(CoroTest):
    '''
    run cmap-dispatch-deadlock.sh
    '''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="CMapDispatchDeadlock"

    def __call__(self, node):
        self.incr("calls")

        result = self.CM.rsh(node, "/usr/share/corosync/tests/cmap-dispatch-deadlock.sh")
        if result is 0:
            return self.success()
        else:
            return self.failure('Deadlock detected')

###################################################################
class SamTest1(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest1"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test1()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

###################################################################
class SamTest2(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest2"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test2()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

###################################################################
class SamTest4(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest4"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test4()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

###################################################################
class SamTest5(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest5"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test5()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

###################################################################
class SamTest6(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest6"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test6()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

###################################################################
class SamTest8(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest8"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test8()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

###################################################################
class SamTest9(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest9"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test9()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure(self.name + ' failed')

class QuorumState(object):
    def __init__(self, cm, node):
        self.node = node
        self.CM = cm
        self.CM.votequorum_agent[self.node].init()

    def refresh(self):
        info = self.CM.votequorum_agent[self.node].votequorum_getinfo()
        assert(info != 'FAIL')
        assert(info != 'NOT_SUPPORTED')

        #self.CM.log('refresh: ' + info)
        params = info.split(':')

        self.node_votes = int(params[0])
        self.expected_votes = int(params[1])
        self.highest_expected = int(params[2])
        self.total_votes = int(params[3])
        self.quorum = int(params[4])
        self.quorate = self.CM.votequorum_agent[self.node].quorum_getquorate()
        assert(self.quorate != 'FAIL')
        assert(self.quorate != 'NOT_SUPPORTED')
        #self.CM.log('quorate: ' + str(self.quorate))

###################################################################
class VoteQuorumBase(CoroTest):

    def setup(self, node):
        ret = CoroTest.setup(self, node)
        self.listener = None
        for n in self.CM.Env["nodes"]:
            if self.listener is None:
                self.listener = n

        return ret

    def config_valid(self, config):
        if config.has_key('totem/rrp_mode'):
            return False
        if config.has_key('quorum/provider'):
            return False
        return True


###################################################################
class VoteQuorumGoDown(VoteQuorumBase):
# all up
# calc min expected votes to get Q
# bring nodes down one-by-one
# confirm cluster looses Q when V < EV
#

    def __init__(self, cm):
        VoteQuorumBase.__init__(self, cm)
        self.name="VoteQuorumGoDown"
        self.victims = []
        self.expected = len(self.CM.Env["nodes"])
        self.config['quorum/provider'] = 'corosync_votequorum'
        self.config['quorum/expected_votes'] = self.expected
        #self.CM.log('set expected to %d' % (self.expected))

    def __call__(self, node):
        self.incr("calls")

        self.victims = []
        pats = []
        pats.append("%s .*VQ notification quorate: 0" % self.listener)
        pats.append("%s .*NQ notification quorate: 0" % self.listener)
        quorum = self.create_watch(pats, 30)
        quorum.setwatch()

        state = QuorumState(self.CM, self.listener)
        state.refresh()
        for n in self.CM.Env["nodes"]:
            if n is self.listener:
                continue

            self.victims.append(n)
            self.CM.StopaCM(n)

            #if not self.wait_for_quorum_change():
            #    return self.failure(self.error_message)
            nodes_alive = len(self.CM.Env["nodes"]) - len(self.victims)
            state.refresh()
            #self.expected = self.expected - 1

            if state.node_votes != 1:
                self.failure('unexpected number of node_votes')

            if state.expected_votes != self.expected:
                self.CM.log('nev: %d != exp %d' % (state.expected_votes, self.expected))
                self.failure('unexpected number of expected_votes')

            if state.total_votes != nodes_alive:
                self.failure('unexpected number of total votes:%d, nodes_alive:%d' % (state.total_votes, nodes_alive))

            min = ((len(self.CM.Env["nodes"]) + 2) / 2)
            if min != state.quorum:
                self.failure('we should have %d (not %d) as quorum' % (min, state.quorum))

            if nodes_alive < state.quorum:
                if state.quorate == 1:
                    self.failure('we should NOT have quorum(%d) %d > %d' % (state.quorate, state.quorum, nodes_alive))
            else:
                if state.quorate == 0:
                    self.failure('we should have quorum(%d) %d <= %d' % (state.quorate, state.quorum, nodes_alive))

        if not quorum.lookforall():
            self.CM.log("Patterns not found: " + repr(quorum.unmatched))
            return self.failure('quorm event not found')

        return self.success()

###################################################################
class VoteQuorumGoUp(VoteQuorumBase):
# all down
# calc min expected votes to get Q
# bring nodes up one-by-one
# confirm cluster gains Q when V >= EV

    def __init__(self, cm):
        VoteQuorumBase.__init__(self, cm)
        self.name="VoteQuorumGoUp"
        self.need_all_up = False
        self.expected = len(self.CM.Env["nodes"])
        self.config['quorum/provider'] = 'corosync_votequorum'
        self.config['quorum/expected_votes'] = self.expected
        #self.CM.log('set expected to %d' % (self.expected))

    def __call__(self, node):
        self.incr("calls")

        pats = []
        pats.append("%s .*VQ notification quorate: 1" % self.listener)
        pats.append("%s .*NQ notification quorate: 1" % self.listener)
        quorum = self.create_watch(pats, 30)
        quorum.setwatch()

        self.CM.StartaCM(self.listener)
        nodes_alive = 1
        state = QuorumState(self.CM, self.listener)
        state.refresh()

        for n in self.CM.Env["nodes"]:
            if n is self.listener:
                continue

            #if not self.wait_for_quorum_change():
            #    return self.failure(self.error_message)

            if state.node_votes != 1:
                self.failure('unexpected number of node_votes')

            if state.expected_votes != self.expected:
                self.CM.log('nev: %d != exp %d' % (state.expected_votes, self.expected))
                self.failure('unexpected number of expected_votes')

            if state.total_votes != nodes_alive:
                self.failure('unexpected number of total votes')

            min = ((len(self.CM.Env["nodes"]) + 2) / 2)
            if min != state.quorum:
                self.failure('we should have %d (not %d) as quorum' % (min, state.quorum))

            if nodes_alive < state.quorum:
                if state.quorate == 1:
                    self.failure('we should NOT have quorum(%d) %d > %d' % (state.quorate, state.quorum, nodes_alive))
            else:
                if state.quorate == 0:
                    self.failure('we should have quorum(%d) %d <= %d' % (state.quorate, state.quorum, nodes_alive))

            self.CM.StartaCM(n)
            nodes_alive = nodes_alive + 1
            state.refresh()

        if not quorum.lookforall():
            self.CM.log("Patterns not found: " + repr(quorum.unmatched))
            return self.failure('quorm event not found')

        return self.success()

###################################################################
class VoteQuorumWaitForAll(VoteQuorumBase):
# all down
# bring nodes up one-by-one
# confirm cluster gains Q when V == num nodes

    def __init__(self, cm):
        VoteQuorumBase.__init__(self, cm)
        self.name="VoteQuorumWaitForAll"
        self.need_all_up = False
        self.expected = len(self.CM.Env["nodes"])
        self.config['quorum/provider'] = 'corosync_votequorum'
        self.config['quorum/expected_votes'] = self.expected
        self.config['quorum/wait_for_all'] = '1'

    def __call__(self, node):
        self.incr("calls")

        pats = []
        pats.append("%s .*VQ notification quorate: 1" % self.listener)
        pats.append("%s .*NQ notification quorate: 1" % self.listener)
        quorum = self.create_watch(pats, 30)
        quorum.setwatch()

        # make absolutly all are stopped
        for n in self.CM.Env["nodes"]:
            self.CM.StopaCM(n)

        # start the listener
        self.CM.StartaCM(self.listener)
        nodes_alive = 1
        state = QuorumState(self.CM, self.listener)
        state.refresh()

        for n in self.CM.Env["nodes"]:
            if n is self.listener:
                continue

            self.CM.StartaCM(n)
            nodes_alive = nodes_alive + 1
            state.refresh()

            if state.node_votes != 1:
                self.failure('unexpected number of node_votes')

            if state.expected_votes != self.expected:
                self.CM.log('nev: %d != exp %d' % (state.expected_votes, self.expected))
                self.failure('unexpected number of expected_votes')

            if state.total_votes != nodes_alive:
                self.failure('unexpected number of total votes')

            if nodes_alive < len(self.CM.Env["nodes"]):
                if state.quorate == 1:
                    self.failure('we should NOT have quorum(%d) %d > %d' % (state.quorate,
                        len(self.CM.Env["nodes"]), nodes_alive))
            else:
                if state.quorate == 0:
                    self.failure('we should have quorum(%d) %d <= %d' % (state.quorate,
                        len(self.CM.Env["nodes"]), nodes_alive))

        if not quorum.lookforall():
            self.CM.log("Patterns not found: " + repr(quorum.unmatched))
            return self.failure('quorm event not found')

        return self.success()

###################################################################
class VoteQuorumContextTest(CoroTest):

    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="VoteQuorumContextTest"
        self.expected = len(self.CM.Env["nodes"])
        self.config['quorum/provider'] = 'corosync_votequorum'
        self.config['quorum/expected_votes'] = self.expected

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.votequorum_agent[node].context_test()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('context_test failed')


###################################################################
class GenSimulStart(CoroTest):
    '''Start all the nodes ~ simultaneously'''

    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenSimulStart"
        self.need_all_up = False
        self.stopall = SimulStopLite(cm)
        self.startall = SimulStartLite(cm)

    def __call__(self, dummy):
        '''Perform the 'SimulStart' test. '''
        self.incr("calls")

        #        We ignore the "node" parameter...

        #        Shut down all the nodes...
        ret = self.stopall(None)
        if not ret:
            return self.failure("Setup failed")

        self.CM.clear_all_caches()

        if not self.startall(None):
            return self.failure("Startall failed")

        return self.success()

###################################################################
class GenSimulStop(CoroTest):
    '''Stop all the nodes ~ simultaneously'''

    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenSimulStop"
        self.startall = SimulStartLite(cm)
        self.stopall = SimulStopLite(cm)
        self.need_all_up = True

    def __call__(self, dummy):
        '''Perform the 'GenSimulStop' test. '''
        self.incr("calls")

        #     We ignore the "node" parameter...

        #     Start up all the nodes...
        ret = self.startall(None)
        if not ret:
            return self.failure("Setup failed")

        if not self.stopall(None):
            return self.failure("Stopall failed")

        return self.success()


class GenFlipTest(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenFlipTest"
        self.test = FlipTest(cm)

    def __call__(self, dummy):
        '''Perform the test. '''
        self.incr("calls")
        return self.test.__call__(dummy)

class GenRestartTest(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenRestartTest"
        self.test = RestartTest(cm)

    def __call__(self, dummy):
        '''Perform the test. '''
        self.incr("calls")
        return self.test.__call__(dummy)

class GenStartOnebyOne(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenStartOnebyOne"
        self.test = RestartOnebyOne(cm)

    def __call__(self, dummy):
        '''Perform the test. '''
        self.incr("calls")
        return self.test.__call__(dummy)

class GenStopOnebyOne(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenStopOnebyOne"
        self.test = StopOnebyOne(cm)

    def __call__(self, dummy):
        '''Perform the test. '''
        self.incr("calls")
        return self.test.__call__(dummy)

class GenRestartOnebyOne(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenRestartOnebyOne"
        self.test = RestartOnebyOne(cm)

    def __call__(self, dummy):
        '''Perform the test. '''
        self.incr("calls")
        return self.test.__call__(dummy)



###################################################################
class GenStopAllBeekhof(CoroTest):
    '''Stop all the nodes ~ simultaneously'''

    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="GenStopAllBeekhof"
        self.need_all_up = True
        self.config['logging/logger_subsys[2]/subsys'] = 'CFG'
        self.config['logging/logger_subsys[2]/debug'] = 'on'

    def __call__(self, node):
        '''Perform the 'GenStopAllBeekhof' test. '''
        self.incr("calls")

        stopping = int(time.time())
        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].pcmk_test()

        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].msg_blaster(1000)

        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].cfg_shutdown()
            self.CM.ShouldBeStatus[n] = "down"

        waited = 0
        max_wait = 60 * 15

        still_up = list(self.CM.Env["nodes"])
        while len(still_up) > 0:
            waited = int(time.time()) - stopping
            self.CM.log("%s still up %s; waited %d secs" % (self.name, str(still_up), waited))
            if waited > max_wait:
                break
            time.sleep(3)
            for v in self.CM.Env["nodes"]:
                if v in still_up:
                    self.CM.ShouldBeStatus[n] = "down"
                    if not self.CM.StataCM(v):
                        still_up.remove(v)

        waited = int(time.time()) - stopping
        if waited > max_wait:
            return self.failure("Waited %d secs for nodes: %s to stop" % (waited, str(still_up)))

        self.CM.log("%s ALL good            (waited %d secs)" % (self.name, waited))
        return self.success()

###################################################################
class NoWDConfig(CoroTest):
    '''Assertion: no config == no watchdog
Setup: no config, kmod inserted
1] make sure watchdog is not enabled
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="NoWDConfig"
        self.need_all_up = False

    def config_valid(self, config):
        return not config.has_key('resources')

    def __call__(self, node):
        '''Perform the 'NoWDConfig' test. '''
        self.incr("calls")

        self.CM.StopaCM(node)
        pats = []
        pats.append("%s .*no resources configured." % node)
        w = self.create_watch(pats, 60)
        w.setwatch()

        self.CM.StartaCM(node)
        if not w.lookforall():
            return self.failure("Patterns not found: " + repr(w.unmatched))
        else:
            return self.success()

###################################################################
class WDConfigNoWd(CoroTest):
    '''Assertion: watchdog config but no watchdog kmod will emit a log
Setup: config watchdog, but no kmod
1] look in the log for warning that there is no kmod
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="WDConfigNoWd"
        self.need_all_up = False

    def __call__(self, node):
        '''Perform the 'WDConfigNoWd' test. '''
        self.incr("calls")

        self.CM.StopaCM(node)
        self.CM.rsh(node, 'rmmod softdog')
        pats = []
        pats.append("%s .*No Watchdog, try modprobe.*" % node)
        w = self.create_watch(pats, 60)
        w.setwatch()

        self.CM.StartaCM(node)
        if not w.lookforall():
            return self.failure("Patterns not found: " + repr(w.unmatched))
        else:
            return self.success()


###################################################################
class NoWDOnCorosyncStop(CoroTest):
    '''Configure WD then /etc/init.d/corosync stop
must stay up for > 60 secs
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="NoWDOnCorosyncStop"
        self.need_all_up = False

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")

        self.CM.StopaCM(node)
        self.CM.rsh(node, 'modprobe softdog')
        self.CM.StartaCM(node)
        pats = []
        pats.append("%s .*Unexpected close, not stopping watchdog.*" % node)
        w = self.create_watch(pats, 60)
        w.setwatch()
        self.CM.StopaCM(node)

        if w.lookforall():
            return self.failure("Should have closed the WD better: " + repr(w.matched))
        else:
            return self.success()


###################################################################
class WDOnForkBomb(CoroTest):
    '''Configure memory resource
run memory leaker / forkbomb
confirm watchdog action
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="WDOnForkBomb"
        self.need_all_up = False
        self.config['logging/logger_subsys[2]/subsys'] = 'WD'
        self.config['logging/logger_subsys[2]/debug'] = 'on'
        self.config['resources/system/memory_used/recovery'] = 'watchdog'
        self.config['resources/system/memory_used/max'] = '80'
        self.config['resources/system/memory_used/poll_period'] = '800'

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")

        # get the uptime
        up_before = self.CM.rsh(node, 'cut -d. -f1 /proc/uptime', 1).rstrip()
        self.CM.StopaCM(node)
        self.CM.rsh(node, 'modprobe softdog')
        self.CM.StartaCM(node)

        self.CM.rsh(node, ':(){ :|:& };:', synchronous=0)

        self.CM.log("wait for it to watchdog")
        time.sleep(60 * 5)

        ping_able = False
        while not ping_able:
            if self.CM.rsh("localhost", "ping -nq -c10 -w10 %s" % node) == 0:
                ping_able = True
                self.CM.log("can ping 10 in 10secs.")
            else:
                self.CM.log("not yet responding to pings.")

        self.CM.ShouldBeStatus[node] = "down"
        # wait for the node to come back up
        self.CM.log("waiting for node to come back up.")
        if self.CM.ns.WaitForNodeToComeUp(node):
            up_after = self.CM.rsh(node, 'cut -d. -f1 /proc/uptime', 1).rstrip()
            if int(up_after) < int(up_before):
                return self.success()
            else:
                return self.failure("node didn't seem to watchdog uptime 1 %s; 2 %s" %(up_before, up_after))
        else:
            return self.failure("node didn't seem to come back up")


###################################################################
class SamWdIntegration1(CoroTest):
    '''start sam hc
kill agent
confirm action
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="SamWdIntegration1"
        self.need_all_up = True
        self.config['logging/logger_subsys[2]/subsys'] = 'WD'
        self.config['logging/logger_subsys[2]/debug'] = 'on'

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")
        self.CM.sam_agent[node].setup_hc()
        pids = self.CM.sam_agent[node].getpid().rstrip().split(" ")

        pats = []
        for pid in pids:
            pats.append('%s .*resource "%s" failed!' % (node, pid))

        w = self.create_watch(pats, 60)
        w.setwatch()

        self.CM.sam_agent[node].kill()

        look_result = w.look()
        if not look_result:
            return self.failure("Patterns not found: " + repr(w.regexes))
        else:
            return self.success()

###################################################################
class SamWdIntegration2(CoroTest):
    '''start sam hc
call sam_stop()
confirm resource "stopped" and no watchdog action.
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="SamWdIntegration2"
        self.need_all_up = True
        self.config['logging/logger_subsys[2]/subsys'] = 'WD'
        self.config['logging/logger_subsys[2]/debug'] = 'on'

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")
        self.CM.sam_agent[node].setup_hc()
        pids = self.CM.sam_agent[node].getpid().rstrip().split(" ")

        no_pats = []
        yes_pats = []
        for pid in pids:
            no_pats.append('%s .*resource "%s" failed!' % (node, pid))
            yes_pats.append('%s .*Fsm:%s event "config_changed", state "running" --> "stopped"' % (node, pid))

        yes_w = self.create_watch(yes_pats, 10)
        no_w = self.create_watch(no_pats, 10)
        yes_w.setwatch()
        no_w.setwatch()
        time.sleep(2)

        self.CM.sam_agent[node].sam_stop()

        yes_matched = yes_w.look()
        no_matched = no_w.look()
        if no_matched:
            return self.failure("Patterns found: " + repr(no_matched))
        else:
            if not yes_matched:
                return self.failure("Patterns NOT found: " + repr(yes_w.regexes))

        return self.success()

###################################################################
class WdDeleteResource(CoroTest):
    '''config resource & start corosync
check that it is getting checked
delete the object resource object
check that we do NOT get watchdog'ed
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="WdDeleteResource"
        self.need_all_up = True
        self.config['logging/logger_subsys[2]/subsys'] = 'MON'
        self.config['logging/logger_subsys[2]/debug'] = 'on'
        self.config['logging/logger_subsys[3]/subsys'] = 'WD'
        self.config['logging/logger_subsys[3]/debug'] = 'on'
        self.config['resources/system/memory_used/recovery'] = 'watchdog'
        self.config['resources/system/memory_used/max'] = '80'
        self.config['resources/system/memory_used/poll_period'] = '800'

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")

        no_pats = []
        yes_pats = []
        no_pats.append('%s .*resource "memory_used" failed!' % node)
        yes_pats.append('%s .*resource "memory_used" deleted from cmap!' % node)
        yes_w = self.create_watch(yes_pats, 10)
        no_w = self.create_watch(no_pats, 10)
        yes_w.setwatch()
        no_w.setwatch()
        time.sleep(2)

        self.CM.rsh(node, 'corosync-cmapctl -D resources.system.memory_used')

        yes_matched = yes_w.look()
        no_matched = no_w.look()
        if no_matched:
            return self.failure("Patterns found: " + repr(no_matched))
        else:
            if not yes_matched:
                return self.failure("Patterns NOT found: " + repr(yes_w.regexes))

        return self.success()


###################################################################
class ResourcePollAdjust(CoroTest):
    '''config resource & start corosync
change the poll_period
check that we do NOT get watchdog'ed
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="ResourcePollAdjust"
        self.need_all_up = True
        self.config['logging/logger_subsys[2]/subsys'] = 'MON'
        self.config['logging/logger_subsys[2]/debug'] = 'on'
        self.config['logging/logger_subsys[3]/subsys'] = 'WD'
        self.config['logging/logger_subsys[3]/debug'] = 'on'
        self.config['resources/system/memory_used/recovery'] = 'none'
        self.config['resources/system/memory_used/max'] = '80'
        self.config['resources/system/memory_used/poll_period'] = '800'

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")

        no_pats = []
        no_pats.append('%s .*resource "memory_used" failed!' % node)
        no_pats.append('%s .*Could NOT use poll_period.*' % node)
        no_w = self.create_watch(no_pats, 10)
        no_w.setwatch()
        changes = 0
        while changes < 50:
            changes = changes + 1
            poll_period = int(random.random() * 5000)
            if poll_period < 500:
                poll_period = 500
            self.CM.log("setting poll_period to: %d" % poll_period)
            self.CM.rsh(node, 'corosync-cmapctl -s resources.system.memory_used.poll_period str %d' % poll_period)
            sleep_time = poll_period * 2 / 1000
            if sleep_time < 1:
                sleep_time = 1
            time.sleep(sleep_time)

        no_matched = no_w.look()
        if no_matched:
            return self.failure("Patterns found: " + repr(no_matched))

        return self.success()


###################################################################
class RebootOnHighMem(CoroTest):
    '''Configure memory resource
run memory leaker / forkbomb
confirm reboot action
'''
    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.name="RebootOnHighMem"
        self.need_all_up = True
        self.config['logging/logger_subsys[2]/subsys'] = 'WD'
        self.config['logging/logger_subsys[2]/debug'] = 'on'
        self.config['resources/system/memory_used/recovery'] = 'reboot'
        self.config['resources/system/memory_used/max'] = '80'
        self.config['resources/system/memory_used/poll_period'] = '800'

    def __call__(self, node):
        '''Perform the test. '''
        self.incr("calls")

        # get the uptime
        up_before = self.CM.rsh(node, 'cut -d. -f1 /proc/uptime', 1).rstrip()
        cmd = 'corosync-cmapctl resources.system.memory_used. | grep current | cut -d= -f2'
        mem_current_str = self.CM.rsh(node, cmd, 1).rstrip()
        mem_new_max = int(mem_current_str) + 5

        self.CM.log("current mem usage: %s, new max:%d" % (mem_current_str, mem_new_max))
        cmd = 'corosync-cmapctl -s resources.system.memory_used.max str ' + str(mem_new_max)
        self.CM.rsh(node, cmd)

        self.CM.rsh(node, 'memhog -r10000 200m', synchronous=0)

        self.CM.log("wait for it to reboot")
        time.sleep(60 * 3)
        cmd = 'corosync-cmapctl resources.system.memory_used. | grep current | cut -d= -f2'
        mem_current_str = self.CM.rsh(node, cmd, 1).rstrip()
        self.CM.log("current mem usage: %s" % (mem_current_str))

        ping_able = False
        while not ping_able:
            if self.CM.rsh("localhost", "ping -nq -c10 -w10 %s" % node) == 0:
                ping_able = True
                self.CM.log("can ping 10 in 10secs.")
            else:
                self.CM.log("not yet responding to pings.")

        self.CM.ShouldBeStatus[node] = "down"
        # wait for the node to come back up
        self.CM.log("waiting for node to come back up.")
        if self.CM.ns.WaitForNodeToComeUp(node):
            up_after = self.CM.rsh(node, 'cut -d. -f1 /proc/uptime', 1).rstrip()
            if int(up_after) < int(up_before):
                return self.success()
            else:
                return self.failure("node didn't seem to watchdog uptime 1 %s; 2 %s" %(up_before, up_after))
        else:
            return self.failure("node didn't seem to come back up")


GenTestClasses = []
GenTestClasses.append(GenSimulStart)
GenTestClasses.append(GenSimulStop)
GenTestClasses.append(GenFlipTest)
GenTestClasses.append(GenRestartTest)
GenTestClasses.append(GenStartOnebyOne)
GenTestClasses.append(GenStopOnebyOne)
GenTestClasses.append(GenRestartOnebyOne)
GenTestClasses.append(GenStopAllBeekhof)
GenTestClasses.append(CpgMsgOrderBasic)
GenTestClasses.append(CpgMsgOrderZcb)
GenTestClasses.append(CpgCfgChgOnExecCrash)
GenTestClasses.append(CpgCfgChgOnGroupLeave)
GenTestClasses.append(CpgCfgChgOnNodeLeave)
GenTestClasses.append(CpgCfgChgOnNodeIsolate)
#GenTestClasses.append(CpgCfgChgOnNodeRestart)

AllTestClasses = []
AllTestClasses.append(CpgContextTest)
AllTestClasses.append(SamTest1)
AllTestClasses.append(SamTest2)
AllTestClasses.append(SamTest4)
AllTestClasses.append(SamTest5)
AllTestClasses.append(SamTest6)
AllTestClasses.append(SamTest8)
AllTestClasses.append(SamTest9)
AllTestClasses.append(SamWdIntegration1)
AllTestClasses.append(SamWdIntegration2)
AllTestClasses.append(NoWDConfig)
AllTestClasses.append(WDConfigNoWd)
AllTestClasses.append(NoWDOnCorosyncStop)
#AllTestClasses.append(WDOnForkBomb)
AllTestClasses.append(WdDeleteResource)
#AllTestClasses.append(RebootOnHighMem)
AllTestClasses.append(ResourcePollAdjust)
AllTestClasses.append(MemLeakObject)
AllTestClasses.append(MemLeakSession)
#AllTestClasses.append(CMapDispatchDeadlock)

# quorum tests
AllTestClasses.append(VoteQuorumContextTest)
GenTestClasses.append(VoteQuorumGoDown)
GenTestClasses.append(VoteQuorumGoUp)
GenTestClasses.append(VoteQuorumWaitForAll)

# FIXME need log messages in sync
#GenTestClasses.append(CpgCfgChgOnLowestNodeJoin)


class ConfigContainer(UserDict):
    def __init__ (self, name):
        self.name = name
        UserDict.__init__(self)

def CoroTestList(cm, audits):
    result = []
    configs = []

    for testclass in AllTestClasses:
        bound_test = testclass(cm)
        if bound_test.is_applicable():
            bound_test.Audits = audits
            result.append(bound_test)

    default = ConfigContainer('default')
    default['logging/fileline'] = 'on'
    default['logging/function_name'] = 'off'
    default['logging/logfile_priority'] = 'info'
    default['logging/syslog_priority'] = 'info'
    default['logging/syslog_facility'] = 'daemon'
    default['uidgid/uid'] = '0'
    default['uidgid/gid'] = '0'
    configs.append(default)

    a = ConfigContainer('none_5min')
    a['totem/token'] = (5 * 60 * 1000)
    a['totem/consensus'] = int(5 * 60 * 1000 * 1.2) + 1
    configs.append(a)

    b = ConfigContainer('pcmk_basic')
    b['totem/token'] = 5000
    b['totem/token_retransmits_before_loss_const'] = 10
    b['totem/join'] = 1000
    b['totem/consensus'] = 7500
    configs.append(b)

    c = ConfigContainer('pcmk_sec_nss')
    c['totem/secauth'] = 'on'
    c['totem/crypto_type'] = 'nss'
    c['totem/token'] = 5000
    c['totem/token_retransmits_before_loss_const'] = 10
    c['totem/join'] = 1000
    c['totem/consensus'] = 7500
    configs.append(c)
#
#    s = ConfigContainer('pcmk_vq')
#    s['quorum/provider'] = 'corosync_votequorum'
#    s['quorum/expected_votes'] = len(cm.Env["nodes"])
#    s['totem/token'] = 5000
#    s['totem/token_retransmits_before_loss_const'] = 10
#    s['totem/join'] = 1000
#    s['totem/vsftype'] = 'none'
#    s['totem/consensus'] = 7500
#    s['totem/max_messages'] = 20
#    configs.append(s)
#
    d = ConfigContainer('sec_nss')
    d['totem/secauth'] = 'on'
    d['totem/crypto_type'] = 'nss'
    configs.append(d)

    if not cm.Env["RrpBindAddr"] is None:
        g = ConfigContainer('rrp_passive')
        g['totem/rrp_mode'] = 'passive'
        g['totem/interface[2]/ringnumber'] = '1'
        g['totem/interface[2]/bindnetaddr'] = cm.Env["RrpBindAddr"]
        g['totem/interface[2]/mcastaddr'] = '226.94.1.2'
        g['totem/interface[2]/mcastport'] = '5405'
        configs.append(g)

        h = ConfigContainer('rrp_active')
        h['totem/rrp_mode'] = 'active'
        h['totem/interface[2]/ringnumber'] = '1'
        h['totem/interface[2]/bindnetaddr'] = cm.Env["RrpBindAddr"]
        h['totem/interface[2]/mcastaddr'] = '226.94.1.2'
        h['totem/interface[2]/mcastport'] = '5405'
        configs.append(h)
    else:
        print 'Not including rrp tests. Use --rrp-binaddr to enable them.'

    num=1
    for cfg in configs:
        for testclass in GenTestClasses:
            bound_test = testclass(cm)
            if bound_test.is_applicable() and bound_test.config_valid(cfg):
                bound_test.Audits = audits
                for c in cfg.keys():
                    bound_test.config[c] = cfg[c]
                bound_test.name = bound_test.name + '_' + cfg.name
                result.append(bound_test)
        num = num + 1

    return result

