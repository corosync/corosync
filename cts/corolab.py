#!/usr/bin/python

'''CTS: Cluster Testing System: Lab environment module
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

import sys
from cts.CTSaudits import AuditList
from cts.CTS import Scenario, InitClusterManager
from corotests import CoroTestList, AllTests
from corosync import *


sys.path.append("/usr/share/pacemaker/tests/cts") # So that things work from the source directory

try:
    from CTSlab import *

except ImportError:
    sys.stderr.write("abort: couldn't find CTSLab in [%s]\n" %
                     ' '.join(sys.path))
    sys.stderr.write("(check your install and PYTHONPATH)\n")
    sys.exit(-1)

tests = None
cm = None
old_handler = None
DefaultFacility = "daemon"


def usage(arg):
    print "Illegal argument " + arg
    print "usage: " + sys.argv[0] +" [options] number-of-iterations" 
    print "\nCommon options: "  
    print "\t [--at-boot (1|0)],         does the cluster software start at boot time" 
    print "\t [--nodes 'node list'],     list of cluster nodes separated by whitespace" 
    print "\t [--limit-nodes max],       only use the first 'max' cluster nodes supplied with --nodes" 
    print "\t [--logfile path],          where should the test software look for logs from cluster nodes" 
    print "\t [--outputfile path],       optional location for the test software to write logs to" 
    print "\t [--syslog-facility name],  which syslog facility should the test software log to" 
    print "\t [--choose testcase-name],  run only the named test" 
    print "\t [--list-tests],            list the valid tests" 
    print "\t [--benchmark],             add the timing information" 
    print "\t "
    print "Additional (less common) options: "  
    print "\t [--trunc (truncate logfile before starting)]" 
    print "\t [--xmit-loss lost-rate(0.0-1.0)]" 
    print "\t [--recv-loss lost-rate(0.0-1.0)]" 
    print "\t [--standby (1 | 0 | yes | no)]" 
    print "\t [--fencing (1 | 0 | yes | no)]" 
    print "\t [--once],                 run all valid tests once" 
    print "\t [--no-loop-tests],        dont run looping/time-based tests" 
    print "\t [--no-unsafe-tests],      dont run tests that are unsafe for use with ocfs2/drbd" 
    print "\t [--valgrind-tests],       include tests using valgrind" 
    print "\t [--experimental-tests],   include experimental tests" 
    print "\t [--oprofile 'node list'], list of cluster nodes to run oprofile on]" 
    print "\t [--qarsh]                 Use the QARSH backdoor to access nodes instead of SSH"
    print "\t [--seed random_seed]"
    print "\t [--set option=value]"
    sys.exit(1)

    
#
# Main entry into the test system.
#
if __name__ == '__main__': 
    Environment = CtsLab()

    NumIter = 0
    Version = 1
    LimitNodes = 0
    TestCase = None
    TruncateLog = 0
    ListTests = 0
    HaveSeed = 0
    node_list = ''

    #
    # The values of the rest of the parameters are now properly derived from
    # the configuration files.
    #
    # Set the signal handler
    signal.signal(15, sig_handler)
    signal.signal(10, sig_handler)
    
    # Process arguments...

    skipthis=None
    args=sys.argv[1:]
    for i in range(0, len(args)):
       if skipthis:
           skipthis=None
           continue

       elif args[i] == "-l" or args[i] == "--limit-nodes":
           skipthis=1
           LimitNodes = int(args[i+1])

       elif args[i] == "-L" or args[i] == "--logfile":
           skipthis=1
           Environment["LogFileName"] = args[i+1]

       elif args[i] == "--outputfile":
           skipthis=1
           Environment["OutputFile"] = args[i+1]

       elif args[i] == "--oprofile":
           skipthis=1
           Environment["oprofile"] = args[i+1].split(' ')

       elif args[i] == "--trunc":
           Environment["TruncateLog"]=1

       elif args[i] == "--list-tests":
           Environment["ListTests"]=1

       elif args[i] == "--benchmark":
           Environment["benchmark"]=1

       elif args[i] == "--qarsh":
           Environment.rsh.enable_qarsh()

       elif args[i] == "--fencing":
           skipthis=1
           if args[i+1] == "1" or args[i+1] == "yes":
               Environment["DoFencing"] = 1
           elif args[i+1] == "0" or args[i+1] == "no":
               Environment["DoFencing"] = 0
           else:
               usage(args[i+1])

       elif args[i] == "--xmit-loss":
           try:
               float(args[i+1])
           except ValueError:
               print ("--xmit-loss parameter should be float")
               usage(args[i+1])
           skipthis=1
           Environment["XmitLoss"] = args[i+1]

       elif args[i] == "--recv-loss":
           try:
               float(args[i+1])
           except ValueError:
               print ("--recv-loss parameter should be float")
               usage(args[i+1])
           skipthis=1
           Environment["RecvLoss"] = args[i+1]

       elif args[i] == "--choose":
           skipthis=1
           TestCase = args[i+1]

       elif args[i] == "--nodes":
           skipthis=1
           node_list = args[i+1].split(' ')

       elif args[i] == "--at-boot" or args[i] == "--cluster-starts-at-boot":
           skipthis=1
           if args[i+1] == "1" or args[i+1] == "yes":
               Environment["at-boot"] = 1
           elif args[i+1] == "0" or args[i+1] == "no":
               Environment["at-boot"] = 0
           else:
               usage(args[i+1])

       elif args[i] == "--set":
           skipthis=1
           (name, value) = args[i+1].split('=')
           Environment[name] = value

       else:
           try:
               NumIter=int(args[i])
           except ValueError:
               usage(args[i])

    Environment["remote_logwatch"]  = True
    Environment["SyslogFacility"] = DefaultFacility
    Environment["loop-minutes"] = int(Environment["loop-minutes"])
    Environment["Stack"]    = "corosync (flatiron)"
    Environment['CMclass']  = corosync_flatiron
    Environment["use_logd"] = 0
    if Environment["OutputFile"]:
        Environment["logger"].append(FileLog(Environment, Environment["OutputFile"]))

    if len(node_list) < 1:
        print "No nodes specified!"
        sys.exit(1)

    if LimitNodes > 0:
        if len(node_list) > LimitNodes:
            print("Limiting the number of nodes configured=%d (max=%d)"
                  %(len(node_list), LimitNodes))
            while len(node_list) > LimitNodes:
                node_list.pop(len(node_list)-1)

    Environment["nodes"] = node_list

    # Create the Cluster Manager object
    cm = Environment['CMclass'](Environment)

    Audits = AuditList(cm)
    Tests = []
        
    # Your basic start up the world type of test scenario...

    # Scenario selection
    scenario = Scenario(
        [ InitClusterManager(Environment), TestAgentComponent(Environment)])


    if Environment["ListTests"] == 1 :
        Tests = CoroTestList(cm, Audits)
        cm.log("Total %d tests"%len(Tests))
        for test in Tests :
            cm.log(str(test.name));
        sys.exit(0)

    if TruncateLog:
        cm.log("Truncating %s" % LogFile)
        lf = open(LogFile, "w");
        if lf != None:
            lf.truncate(0)
            lf.close()

    keys = []
    for key in Environment.keys():
        keys.append(key)

    keys.sort()
    for key in keys:
        cm.debug("Environment["+key+"]:\t"+str(Environment[key]))

    cm.log(">>>>>>>>>>>>>>>> BEGINNING " + repr(NumIter) + " TESTS ")
    cm.log("System log files: %s" % Environment["LogFileName"])
    cm.ns.WaitForAllNodesToComeUp(Environment["nodes"])
    cm.log("Cluster nodes: ")
    for node in Environment["nodes"]:
        cm.log("    * %s" % (node))

    if TestCase != None:
        for test in CoroTestList(cm, Audits):
            if test.name == TestCase:
                Tests.append(test)
        if Tests == []:
            usage("--choose: No applicable/valid tests chosen")        
    else:
        Tests = CoroTestList(cm, Audits)
    
    if Environment["benchmark"]:
        Environment.ScenarioTests = BenchTests(scenario, cm, Tests, Audits)
    elif Environment["all-once"] or NumIter == 0:
        Environment.ScenarioTests = AllTests(scenario, cm, Tests, Audits)
    else:
        Environment.ScenarioTests = RandomTests(scenario, cm, Tests, Audits)

    try :
        overall, detailed = Environment.ScenarioTests.run(NumIter)
    except :
        cm.Env.log("Exception by %s" % sys.exc_info()[0])
        for logmethod in Environment["logger"]:
          traceback.print_exc(50, logmethod)
        
    Environment.ScenarioTests.summarize()
    if Environment.ScenarioTests.Stats["failure"] > 0:
        sys.exit(Environment.ScenarioTests.Stats["failure"])

    elif Environment.ScenarioTests.Stats["success"] != NumIter:
        cm.Env.log("No failure count but success != requested iterations")
        sys.exit(1)
        
