from m5.SimObject import SimObject
from m5.params import *
from m5.proxy import *
class Process(SimObject):
    type = 'Process'
    abstract = True
    output = Param.String('cout', 'filename for stdout/stderr')
    system = Param.System(Parent.any, "system process will run on")

class LiveProcess(Process):
    type = 'LiveProcess'
    executable = Param.String('', "executable (overrides cmd[0] if set)")
    cmd = VectorParam.String("command line (executable plus arguments)")
    env = VectorParam.String('', "environment settings")
    cwd = Param.String('', "current working directory")
    input = Param.String('cin', "filename for stdin")
    uid = Param.Int(100, 'user id')
    euid = Param.Int(100, 'effective user id')
    gid = Param.Int(100, 'group id')
    egid = Param.Int(100, 'effective group id')
    pid = Param.Int(100, 'process id')
    ppid = Param.Int(99, 'parent process id')

class AlphaLiveProcess(LiveProcess):
    type = 'AlphaLiveProcess'

class SparcLiveProcess(LiveProcess):
    type = 'SparcLiveProcess'

class MipsLiveProcess(LiveProcess):
    type = 'MipsLiveProcess'

class EioProcess(Process):
    type = 'EioProcess'
    chkpt = Param.String('', "EIO checkpoint file name (optional)")
    file = Param.String("EIO trace file name")
