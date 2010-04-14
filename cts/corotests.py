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

from cts.CTStests import *

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
        self.need_all_up = True

    def setup(self, node):
        ret = CTSTest.setup(self, node)

        # setup the authkey
        localauthkey = '/tmp/authkey'
        if not os.path.exists(localauthkey):
            self.CM.rsh(node, 'corosync-keygen')
            self.CM.rsh.cp("%s:%s" % (node, "/etc/corosync/authkey"), localauthkey)

        for n in self.CM.Env["nodes"]:
            if n is not node:
                #copy key onto other nodes
                self.CM.rsh.cp(localauthkey, "%s:%s" % (n, "/etc/corosync/authkey"))

        # copy over any new config
        for c in self.config:
            self.CM.new_config[c] = self.config[c]

        # apply the config
        self.CM.apply_new_config()

        # start/stop all corosyncs'
        for n in self.CM.Env["nodes"]:
            if self.need_all_up and not self.CM.StataCM(n):
                self.incr("started")
                self.start(n)
            if not self.need_all_up and self.CM.StataCM(n):
                self.incr("stopped")
                self.stop(n)
        return ret


    def teardown(self, node):
        self.CM.apply_default_config()
        return CTSTest.teardown(self, node)


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
            self.CM.cpg_agent[n].clean_start()
            self.CM.cpg_agent[n].cpg_join(self.name)
            if self.listener is None:
                self.listener = n
            elif self.wobbly is None:
                self.wobbly = n

        self.wobbly_id = self.CM.cpg_agent[self.wobbly].cpg_local_get()
        self.CM.cpg_agent[self.listener].record_config_events(truncate=True)

        return ret

    def wait_for_config_change(self):
        found = False
        max_timeout = 5 * 60
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
                        print 'waited 60 seconds'
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
        self.CM.cpg_agent[self.wobbly].cpg_leave(self.name)

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
        self.config['compatibility'] = 'none'

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
                self.CM.cpg_agent[n].cpg_join(self.name)

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
        self.CM.cpg_agent[self.lowest].cpg_join(self.name)
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
class CpgMsgOrderBase(CoroTest):

    def __init__(self, cm):
        CoroTest.__init__(self,cm)
        self.num_msgs_per_node = 0
        self.total_num_msgs = 0

    def setup(self, node):
        ret = CoroTest.setup(self, node)

        for n in self.CM.Env["nodes"]:
            self.total_num_msgs = self.total_num_msgs + self.num_msgs_per_node
            self.CM.cpg_agent[n].clean_start()
            self.CM.cpg_agent[n].cpg_join(self.name)
            self.CM.cpg_agent[n].record_messages()

        time.sleep(1)
        return ret

    def cpg_msg_blaster(self):
        for n in self.CM.Env["nodes"]:
            self.CM.cpg_agent[n].msg_blaster(self.num_msgs_per_node)
        
    def wait_and_validate_order(self):
        msgs = {}

        for n in self.CM.Env["nodes"]:
            msgs[n] = []
            stopped = False
            waited = 0

            while len(msgs[n]) < self.total_num_msgs and waited < 360:

                msg = self.CM.cpg_agent[n].read_messages(50)
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
class ServiceLoadTest(CoroTest):
    '''
    Test loading and unloading of service engines
    '''
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="ServiceLoadTest"

    def is_loaded(self, node):
        check = 'corosync-objctl runtime.services. | grep evs'

        (res, out) = self.CM.rsh(node, check, stdout=2)
        if res is 0:
            return True
        else:
            return False

    def service_unload(self, node):
        # unload evs
        pats = []
        pats.append("%s .*Service engine unloaded: corosync extended.*" % node)
        unloaded = self.create_watch(pats, 60)
        unloaded.setwatch()

        self.CM.rsh(node, 'corosync-cfgtool -u corosync_evs')

        if not unloaded.lookforall():
            self.CM.log("Patterns not found: " + repr(unloaded.unmatched))
            self.error_message = "evs service not unloaded"
            return False

        if self.is_loaded(node):
            self.error_message = "evs has been unload, why are it's session objects are still there?"
            return False
        return True

    def service_load(self, node):
        # now reload it.
        pats = []
        pats.append("%s .*Service engine loaded.*" % node)
        loaded = self.create_watch(pats, 60)
        loaded.setwatch()

        self.CM.rsh(node, 'corosync-cfgtool -l corosync_evs')

        if not loaded.lookforall():
            self.CM.log("Patterns not found: " + repr(loaded.unmatched))
            self.error_message = "evs service not unloaded"
            return False

        return True


    def __call__(self, node):
        self.incr("calls")
        should_be_loaded = True

        if self.is_loaded(node):
            ret = self.service_unload(node)
            should_be_loaded = False
        else:
            ret = self.service_load(node)
            should_be_loaded = True

        if not ret:
            return self.failure(self.error_message)

        if self.is_loaded(node):
            ret = self.service_unload(node)
        else:
            ret = self.service_load(node)

        if not ret:
            return self.failure(self.error_message)

        return self.success()


###################################################################
class ConfdbReplaceTest(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="ConfdbReplaceTest"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.confdb_agent[node].set_get_test()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('set_get_test failed')


###################################################################
class ConfdbIncrementTest(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="ConfdbIncrementTest"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.confdb_agent[node].increment_decrement_test()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('increment_decrement_test failed')


###################################################################
class ConfdbObjectFindTest(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="ConfdbObjectFindTest"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.confdb_agent[node].object_find_test()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('object_find_test failed')


###################################################################
class ConfdbNotificationTest(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="ConfdbNotificationTest"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.confdb_agent[node].notification_test()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('notification_test failed')

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
            return self.failure('sam test 1 failed')

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
            return self.failure('sam test 2 failed')

###################################################################
class SamTest3(CoroTest):
    def __init__(self, cm):
        CoroTest.__init__(self, cm)
        self.name="SamTest3"

    def __call__(self, node):
        self.incr("calls")
        res = self.CM.sam_agent[node].test3()
        if 'OK' in res:
            return self.success()
        else:
            return self.failure('sam test 3 failed')

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
            return self.failure('sam test 4 failed')


class QuorumState(object):
    def __init__(self, cm, node):
        self.node = node
        self.CM = cm

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
    '''
    '''

    def setup(self, node):
        ret = CoroTest.setup(self, node)
        self.id_map = {}
        self.listener = None
        for n in self.CM.Env["nodes"]:
            if self.listener is None:
                self.listener = n
            if self.need_all_up:
                self.CM.cpg_agent[n].clean_start()
                self.CM.cpg_agent[n].cpg_join(self.name)
                self.id_map[n] = self.CM.cpg_agent[n].cpg_local_get()

        #self.CM.votequorum_agent[self.listener].record_events()
        return ret

    def wait_for_quorum_change(self):
        found = False
        max_timeout = 5 * 60
        waited = 0

        printit = 0
        self.CM.log("Waiting for quorum event on " + self.listener)
        while not found:
            try:
                event = self.CM.votequorum_agent[self.listener].read_event()
            except:
                return self.failure('connection to test agent failed.')
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
                        print 'waited 60 seconds'
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

# repeat below with equal and uneven votes

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

        state = QuorumState(self.CM, self.listener)
        for n in self.CM.Env["nodes"]:
            if n is self.listener:
                continue

            self.victims.append(n)
            self.CM.StopaCM(n)

            nodes_alive = len(self.CM.Env["nodes"]) - len(self.victims)
            state.refresh()
            #self.expected = self.expected - 1

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

        return self.success()


# all down
# calc min expected votes to get Q
# bring nodes up one-by-one
# confirm cluster gains Q when V >= EV
#
###################################################################
class VoteQuorumGoUp(VoteQuorumBase):
# all up
# calc min expected votes to get Q
# bring nodes down one-by-one
# confirm cluster looses Q when V < EV
#

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

        self.CM.StartaCM(self.listener)
        nodes_alive = 1
        state = QuorumState(self.CM, self.listener)
        state.refresh()

        for n in self.CM.Env["nodes"]:
            if n is self.listener:
                continue

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

        return self.success()


GenTestClasses = []
GenTestClasses.append(CpgMsgOrderBasic)
GenTestClasses.append(CpgMsgOrderZcb)
GenTestClasses.append(CpgCfgChgOnExecCrash)
GenTestClasses.append(CpgCfgChgOnGroupLeave)
GenTestClasses.append(CpgCfgChgOnNodeLeave)
GenTestClasses.append(CpgCfgChgOnNodeIsolate)
GenTestClasses.append(CpgCfgChgOnLowestNodeJoin)
GenTestClasses.append(VoteQuorumGoDown)
GenTestClasses.append(VoteQuorumGoUp)

AllTestClasses = []
AllTestClasses.append(ConfdbReplaceTest)
AllTestClasses.append(ConfdbIncrementTest)
AllTestClasses.append(ConfdbObjectFindTest)
AllTestClasses.append(ConfdbNotificationTest)
AllTestClasses.append(SamTest1)
AllTestClasses.append(SamTest2)
AllTestClasses.append(SamTest3)
AllTestClasses.append(SamTest4)
AllTestClasses.append(ServiceLoadTest)
AllTestClasses.append(MemLeakObject)
AllTestClasses.append(MemLeakSession)

AllTestClasses.append(FlipTest)
AllTestClasses.append(RestartTest)
AllTestClasses.append(StartOnebyOne)
AllTestClasses.append(SimulStart)
AllTestClasses.append(StopOnebyOne)
AllTestClasses.append(SimulStop)
AllTestClasses.append(RestartOnebyOne)


def CoroTestList(cm, audits):
    result = []
    configs = []

    for testclass in AllTestClasses:
        bound_test = testclass(cm)
        if bound_test.is_applicable():
            bound_test.Audits = audits
            result.append(bound_test)

    default = {}
    default['logging/function_name'] = 'off'
    default['logging/logfile_priority'] = 'info'
    default['logging/syslog_priority'] = 'info'
    default['logging/syslog_facility'] = 'daemon'
    default['uidgid/uid'] = '0'
    default['uidgid/gid'] = '0'
    configs.append(default)

    a = {}
    a['compatibility'] = 'none'
    a['totem/token'] = 10000
    configs.append(a)

    b = {}
    b['compatibility'] = 'whitetank'
    b['totem/token'] = 10000
    configs.append(b)

    c = {}
    c['totem/secauth'] = 'on'
    c['totem/crypto_accept'] = 'new'
    c['totem/crypto_type'] = 'nss'
    configs.append(c)

    d = {}
    d['totem/secauth'] = 'on'
    d['totem/crypto_type'] = 'sober'
    configs.append(d)

    e = {}
    e['totem/threads'] = 4
    configs.append(e)

    #quorum/provider=
    #f = {}
    #f['quorum/provider'] = 'corosync_quorum_ykd'
    #configs.append(f)

    if not cm.Env["RrpBindAddr"] is None:
        g = {}
        g['totem/rrp_mode'] = 'passive'
        g['totem/interface[2]/ringnumber'] = '1'
        g['totem/interface[2]/bindnetaddr'] = cm.Env["RrpBindAddr"]
        g['totem/interface[2]/mcastaddr'] = '226.94.1.2'
        g['totem/interface[2]/mcastport'] = '5405'
        configs.append(g)

        h = {}
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
            if bound_test.is_applicable():
                bound_test.Audits = audits
                for c in cfg:
                    bound_test.config[c] = cfg[c]
                bound_test.name = bound_test.name + '_' + str(num)
                result.append(bound_test)
        num = num + 1

    return result

