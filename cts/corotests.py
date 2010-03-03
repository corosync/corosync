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

    def setup(self, node):
        ret = CTSTest.setup(self, node)
        self.CM.apply_new_config()

        for n in self.CM.Env["nodes"]:
            if not self.CM.StataCM(n):
                self.incr("started")
                self.start(n)
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
            self.CM.agent[n].clean_start()
            self.CM.agent[n].cpg_join(self.name)
            if self.listener is None:
                self.listener = n
            elif self.wobbly is None:
                self.wobbly = n

        self.wobbly_id = self.CM.agent[self.wobbly].cpg_local_get()
        self.CM.agent[self.listener].record_config_events(truncate=True)

        return ret

    def wait_for_config_change(self):
        found = False
        max_timeout = 5 * 60
        waited = 0
        printit = 0
        self.CM.log("Waiting for config change on " + self.listener)
        while not found:
            event = self.CM.agent[self.listener].read_config_event()
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
        self.CM.agent[self.wobbly].cpg_leave(self.name)

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
class CpgCfgChgOnExecCrash(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnExecCrash"

    def failure_action(self):
        self.CM.log("sending SIGSEGV to corosync on " + self.wobbly)
        self.CM.rsh(self.wobbly, "killall -SIGSEGV corosync")
        self.CM.rsh(self.wobbly, "rm -f /var/run/corosync.pid")

    def __call__(self, node):
        self.incr("calls")
        self.failure_action()
        return self.wait_for_config_change()


###################################################################
class CpgCfgChgOnNodeLeave_v2(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnNodeLeave_v2"
       
    def setup(self, node):
        self.CM.new_config['compatibility'] = 'none'
        self.CM.new_config['totem/token'] = 10000
        return CpgConfigChangeBase.setup(self, node)

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
class CpgCfgChgOnNodeLeave_v1(CpgConfigChangeBase):

    def __init__(self, cm):
        CpgConfigChangeBase.__init__(self,cm)
        self.name="CpgCfgChgOnNodeLeave_v1"

    def setup(self, node):
        self.CM.new_config['compatibility'] = 'whitetank'
        self.CM.new_config['totem/token'] = 10000
        return CpgConfigChangeBase.setup(self, node)
        
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
            self.CM.agent[n].clean_start()
            self.CM.agent[n].cpg_join(self.name)
            self.CM.agent[n].record_messages()

        time.sleep(1)
        return ret

    def cpg_msg_blaster(self):
        for n in self.CM.Env["nodes"]:
            self.CM.agent[n].msg_blaster(self.num_msgs_per_node)
        
    def wait_and_validate_order(self):
        msgs = {}

        for n in self.CM.Env["nodes"]:
            msgs[n] = []
            got = False
            stopped = False
            self.CM.debug( " getting messages from " + n )

            while len(msgs[n]) < self.total_num_msgs and not stopped:

                msg = self.CM.agent[n].read_messages(25)
                if not msg == None:
                    got = True
                    msgl = msg.split(";")

                    # remove empty entries
                    not_done=True
                    while not_done:
                        try:
                            msgl.remove('')
                        except:
                            not_done = False

                    msgs[n].extend(msgl)
                elif msg == None and got:
                    self.CM.debug(" done getting messages from " + n)
                    stopped = True

                if not got:
                    time.sleep(1)

        fail = False
        for i in range(0, self.total_num_msgs):
            first = None
            for n in self.CM.Env["nodes"]:
                if first == None:
                    first = n
                else:
                    if not msgs[first][i] == msgs[n][i]:
                        # message order not the same!
                        fail = True
                        self.CM.log(msgs[first][i] + " != " + msgs[n][i])
                
        if fail:
            return self.failure()
        else:
            return self.success()

###################################################################
class CpgMsgOrderBasic(CpgMsgOrderBase):
    '''
    each sends & logs 100 messages
    '''
    def __init__(self, cm):
        CpgMsgOrderBase.__init__(self,cm)
        self.name="CpgMsgOrderBasic"

    def __call__(self, node):
        self.incr("calls")

        # o > reconfigure corosync
        # o > reconfigure interfaces (mtu)
        # o > restart corosync
        # o > set node to die after x msgs
        self.num_msgs_per_node = 100
        self.cpg_msg_blaster()
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



AllTestClasses = []
AllTestClasses.append(MemLeakObject)
AllTestClasses.append(MemLeakSession)
AllTestClasses.append(CpgCfgChgOnGroupLeave)
AllTestClasses.append(CpgCfgChgOnNodeLeave)
AllTestClasses.append(CpgCfgChgOnNodeLeave_v1)
AllTestClasses.append(CpgCfgChgOnNodeLeave_v2)
AllTestClasses.append(CpgCfgChgOnExecCrash)
AllTestClasses.append(CpgMsgOrderBasic)

AllTestClasses.append(FlipTest)
AllTestClasses.append(RestartTest)
AllTestClasses.append(StartOnebyOne)
AllTestClasses.append(SimulStart)
AllTestClasses.append(StopOnebyOne)
AllTestClasses.append(SimulStop)
AllTestClasses.append(RestartOnebyOne)
#AllTestClasses.append(PartialStart)


def CoroTestList(cm, audits):
    result = []
    for testclass in AllTestClasses:
        bound_test = testclass(cm)
        if bound_test.is_applicable():
            bound_test.Audits = audits
            result.append(bound_test)
    return result

