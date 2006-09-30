/*
 * Copyright (c) 2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Kevin Lim
 *          Nathan Binkert
 */

#include "config/full_system.hh"
#include "config/use_checker.hh"

#include "arch/isa_traits.hh" // For MachInst
#include "base/trace.hh"
#include "cpu/base.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/thread_context.hh"
#include "cpu/exetrace.hh"
#include "cpu/ozone/cpu.hh"
#include "cpu/quiesce_event.hh"
#include "cpu/static_inst.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"

#if FULL_SYSTEM
#include "arch/faults.hh"
#include "arch/alpha/osfpal.hh"
#include "arch/alpha/tlb.hh"
#include "arch/alpha/types.hh"
#include "arch/vtophys.hh"
#include "base/callback.hh"
//#include "base/remote_gdb.hh"
#include "cpu/profile.hh"
#include "kern/kernel_stats.hh"
#include "mem/physical.hh"
#include "sim/faults.hh"
#include "sim/sim_events.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"
#else // !FULL_SYSTEM
#include "sim/process.hh"
#endif // FULL_SYSTEM

#if USE_CHECKER
#include "cpu/checker/thread_context.hh"
#endif

using namespace TheISA;

template <class Impl>
template<typename T>
void
OzoneCPU<Impl>::trace_data(T data) {
    if (traceData) {
        traceData->setData(data);
    }
}

template <class Impl>
OzoneCPU<Impl>::TickEvent::TickEvent(OzoneCPU *c, int w)
    : Event(&mainEventQueue, CPU_Tick_Pri), cpu(c), width(w)
{
}

template <class Impl>
void
OzoneCPU<Impl>::TickEvent::process()
{
    cpu->tick();
}

template <class Impl>
const char *
OzoneCPU<Impl>::TickEvent::description()
{
    return "OzoneCPU tick event";
}

template <class Impl>
OzoneCPU<Impl>::OzoneCPU(Params *p)
#if FULL_SYSTEM
    : BaseCPU(p), thread(this, 0), tickEvent(this, p->width),
#else
    : BaseCPU(p), thread(this, 0, p->workload[0], 0, p->mem),
      tickEvent(this, p->width),
#endif
      mem(p->mem), comm(5, 5)
{
    frontEnd = new FrontEnd(p);
    backEnd = new BackEnd(p);

    _status = Idle;

    if (p->checker) {
#if USE_CHECKER
        BaseCPU *temp_checker = p->checker;
        checker = dynamic_cast<Checker<DynInstPtr> *>(temp_checker);
        checker->setMemory(mem);
#if FULL_SYSTEM
        checker->setSystem(p->system);
#endif
        checkerTC = new CheckerThreadContext<OzoneTC>(&ozoneTC, checker);
        thread.tc = checkerTC;
        tc = checkerTC;
#else
        panic("Checker enabled but not compiled in!");
#endif
    } else {
        checker = NULL;
        thread.tc = &ozoneTC;
        tc = &ozoneTC;
    }

    ozoneTC.cpu = this;
    ozoneTC.thread = &thread;

    thread.inSyscall = false;

    thread.setStatus(ThreadContext::Suspended);
#if FULL_SYSTEM
    /***** All thread state stuff *****/
    thread.cpu = this;
    thread.setTid(0);

    thread.quiesceEvent = new EndQuiesceEvent(tc);

    system = p->system;
    itb = p->itb;
    dtb = p->dtb;
    physmem = p->system->physmem;

    if (p->profile) {
        thread.profile = new FunctionProfile(p->system->kernelSymtab);
        // @todo: This might be better as an ThreadContext instead of OzoneTC
        Callback *cb =
            new MakeCallback<OzoneTC,
            &OzoneTC::dumpFuncProfile>(&ozoneTC);
        registerExitCallback(cb);
    }

    // let's fill with a dummy node for now so we don't get a segfault
    // on the first cycle when there's no node available.
    static ProfileNode dummyNode;
    thread.profileNode = &dummyNode;
    thread.profilePC = 3;
#else
    thread.cpu = this;
#endif // !FULL_SYSTEM

    numInst = 0;
    startNumInst = 0;

    threadContexts.push_back(tc);

    frontEnd->setCPU(this);
    backEnd->setCPU(this);

    frontEnd->setTC(tc);
    backEnd->setTC(tc);

    frontEnd->setThreadState(&thread);
    backEnd->setThreadState(&thread);

    frontEnd->setCommBuffer(&comm);
    backEnd->setCommBuffer(&comm);

    frontEnd->setBackEnd(backEnd);
    backEnd->setFrontEnd(frontEnd);

    decoupledFrontEnd = p->decoupledFrontEnd;

    globalSeqNum = 1;

    checkInterrupts = false;

    for (int i = 0; i < TheISA::TotalNumRegs; ++i) {
        thread.renameTable[i] = new DynInst(this);
        thread.renameTable[i]->setResultReady();
    }

    frontEnd->renameTable.copyFrom(thread.renameTable);
    backEnd->renameTable.copyFrom(thread.renameTable);

#if !FULL_SYSTEM
    /* Use this port to for syscall emulation writes to memory. */
    Port *mem_port;
    TranslatingPort *trans_port;
    trans_port = new TranslatingPort(csprintf("%s-%d-funcport",
                                              name(), 0),
                                     p->workload[0]->pTable,
                                     false);
    mem_port = p->mem->getPort("functional");
    mem_port->setPeer(trans_port);
    trans_port->setPeer(mem_port);
    thread.setMemPort(trans_port);
#else
    Port *mem_port;
    FunctionalPort *phys_port;
    VirtualPort *virt_port;
    phys_port = new FunctionalPort(csprintf("%s-%d-funcport",
                                            name(), 0));
    mem_port = system->physmem->getPort("functional");
    mem_port->setPeer(phys_port);
    phys_port->setPeer(mem_port);

    virt_port = new VirtualPort(csprintf("%s-%d-vport",
                                         name(), 0));
    mem_port = system->physmem->getPort("functional");
    mem_port->setPeer(virt_port);
    virt_port->setPeer(mem_port);

    thread.setPhysPort(phys_port);
    thread.setVirtPort(virt_port);
#endif

    lockFlag = 0;

    DPRINTF(OzoneCPU, "OzoneCPU: Created Ozone cpu object.\n");
}

template <class Impl>
OzoneCPU<Impl>::~OzoneCPU()
{
}

template <class Impl>
void
OzoneCPU<Impl>::switchOut()
{
    switchCount = 0;
    // Front end needs state from back end, so switch out the back end first.
    backEnd->switchOut();
    frontEnd->switchOut();
}

template <class Impl>
void
OzoneCPU<Impl>::signalSwitched()
{
    if (++switchCount == 2) {
        backEnd->doSwitchOut();
        frontEnd->doSwitchOut();
#if USE_CHECKER
        if (checker)
            checker->switchOut();
#endif

        _status = SwitchedOut;
        if (tickEvent.scheduled())
            tickEvent.squash();
    }
    assert(switchCount <= 2);
}

template <class Impl>
void
OzoneCPU<Impl>::takeOverFrom(BaseCPU *oldCPU)
{
    BaseCPU::takeOverFrom(oldCPU);

    backEnd->takeOverFrom();
    frontEnd->takeOverFrom();
    assert(!tickEvent.scheduled());

    // @todo: Fix hardcoded number
    // Clear out any old information in time buffer.
    for (int i = 0; i < 6; ++i) {
        comm.advance();
    }

    // if any of this CPU's ThreadContexts are active, mark the CPU as
    // running and schedule its tick event.
    for (int i = 0; i < threadContexts.size(); ++i) {
        ThreadContext *tc = threadContexts[i];
        if (tc->status() == ThreadContext::Active &&
            _status != Running) {
            _status = Running;
            tickEvent.schedule(curTick);
        }
    }
    // Nothing running, change status to reflect that we're no longer
    // switched out.
    if (_status == SwitchedOut) {
        _status = Idle;
    }
}

template <class Impl>
void
OzoneCPU<Impl>::activateContext(int thread_num, int delay)
{
    // Eventually change this in SMT.
    assert(thread_num == 0);

    assert(_status == Idle);
    notIdleFraction++;
    scheduleTickEvent(delay);
    _status = Running;
    thread.setStatus(ThreadContext::Active);
    frontEnd->wakeFromQuiesce();
}

template <class Impl>
void
OzoneCPU<Impl>::suspendContext(int thread_num)
{
    // Eventually change this in SMT.
    assert(thread_num == 0);
    // @todo: Figure out how to initially set the status properly so
    // this is running.
//    assert(_status == Running);
    notIdleFraction--;
    unscheduleTickEvent();
    _status = Idle;
}

template <class Impl>
void
OzoneCPU<Impl>::deallocateContext(int thread_num, int delay)
{
    // for now, these are equivalent
    suspendContext(thread_num);
}

template <class Impl>
void
OzoneCPU<Impl>::haltContext(int thread_num)
{
    // for now, these are equivalent
    suspendContext(thread_num);
}

template <class Impl>
void
OzoneCPU<Impl>::regStats()
{
    using namespace Stats;

    BaseCPU::regStats();

    thread.numInsts
        .name(name() + ".num_insts")
        .desc("Number of instructions executed")
        ;

    thread.numMemRefs
        .name(name() + ".num_refs")
        .desc("Number of memory references")
        ;

    notIdleFraction
        .name(name() + ".not_idle_fraction")
        .desc("Percentage of non-idle cycles")
        ;

    idleFraction
        .name(name() + ".idle_fraction")
        .desc("Percentage of idle cycles")
        ;

    quiesceCycles
        .name(name() + ".quiesce_cycles")
        .desc("Number of cycles spent in quiesce")
        ;

    idleFraction = constant(1.0) - notIdleFraction;

    frontEnd->regStats();
    backEnd->regStats();
}

template <class Impl>
void
OzoneCPU<Impl>::resetStats()
{
    startNumInst = numInst;
    notIdleFraction = (_status != Idle);
}

template <class Impl>
void
OzoneCPU<Impl>::init()
{
    BaseCPU::init();

    // Mark this as in syscall so it won't need to squash
    thread.inSyscall = true;
#if FULL_SYSTEM
    for (int i = 0; i < threadContexts.size(); ++i) {
        ThreadContext *tc = threadContexts[i];

        // initialize CPU, including PC
        TheISA::initCPU(tc, tc->readCpuId());
    }
#endif
    frontEnd->renameTable.copyFrom(thread.renameTable);
    backEnd->renameTable.copyFrom(thread.renameTable);

    thread.inSyscall = false;
}

template <class Impl>
Port *
OzoneCPU<Impl>::getPort(const std::string &if_name, int idx)
{
    if (if_name == "dcache_port")
        return backEnd->getDcachePort();
    else if (if_name == "icache_port")
        return frontEnd->getIcachePort();
    else
        panic("No Such Port\n");
}

template <class Impl>
void
OzoneCPU<Impl>::serialize(std::ostream &os)
{
    BaseCPU::serialize(os);
    SERIALIZE_ENUM(_status);
    nameOut(os, csprintf("%s.tc", name()));
    ozoneTC.serialize(os);
    nameOut(os, csprintf("%s.tickEvent", name()));
    tickEvent.serialize(os);
}

template <class Impl>
void
OzoneCPU<Impl>::unserialize(Checkpoint *cp, const std::string &section)
{
    BaseCPU::unserialize(cp, section);
    UNSERIALIZE_ENUM(_status);
    ozoneTC.unserialize(cp, csprintf("%s.tc", section));
    tickEvent.unserialize(cp, csprintf("%s.tickEvent", section));
}

template <class Impl>
Fault
OzoneCPU<Impl>::copySrcTranslate(Addr src)
{
    panic("Copy not implemented!\n");
    return NoFault;
#if 0
    static bool no_warn = true;
    int blk_size = (dcacheInterface) ? dcacheInterface->getBlockSize() : 64;
    // Only support block sizes of 64 atm.
    assert(blk_size == 64);
    int offset = src & (blk_size - 1);

    // Make sure block doesn't span page
    if (no_warn &&
        (src & TheISA::PageMask) != ((src + blk_size) & TheISA::PageMask) &&
        (src >> 40) != 0xfffffc) {
        warn("Copied block source spans pages %x.", src);
        no_warn = false;
    }

    memReq->reset(src & ~(blk_size - 1), blk_size);

    // translate to physical address
    Fault fault = tc->translateDataReadReq(memReq);

    assert(fault != Alignment_Fault);

    if (fault == NoFault) {
        tc->copySrcAddr = src;
        tc->copySrcPhysAddr = memReq->paddr + offset;
    } else {
        tc->copySrcAddr = 0;
        tc->copySrcPhysAddr = 0;
    }
    return fault;
#endif
}

template <class Impl>
Fault
OzoneCPU<Impl>::copy(Addr dest)
{
    panic("Copy not implemented!\n");
    return NoFault;
#if 0
    static bool no_warn = true;
    int blk_size = (dcacheInterface) ? dcacheInterface->getBlockSize() : 64;
    // Only support block sizes of 64 atm.
    assert(blk_size == 64);
    uint8_t data[blk_size];
    //assert(tc->copySrcAddr);
    int offset = dest & (blk_size - 1);

    // Make sure block doesn't span page
    if (no_warn &&
        (dest & TheISA::PageMask) != ((dest + blk_size) & TheISA::PageMask) &&
        (dest >> 40) != 0xfffffc) {
        no_warn = false;
        warn("Copied block destination spans pages %x. ", dest);
    }

    memReq->reset(dest & ~(blk_size -1), blk_size);
    // translate to physical address
    Fault fault = tc->translateDataWriteReq(memReq);

    assert(fault != Alignment_Fault);

    if (fault == NoFault) {
        Addr dest_addr = memReq->paddr + offset;
        // Need to read straight from memory since we have more than 8 bytes.
        memReq->paddr = tc->copySrcPhysAddr;
        tc->mem->read(memReq, data);
        memReq->paddr = dest_addr;
        tc->mem->write(memReq, data);
        if (dcacheInterface) {
            memReq->cmd = Copy;
            memReq->completionEvent = NULL;
            memReq->paddr = tc->copySrcPhysAddr;
            memReq->dest = dest_addr;
            memReq->size = 64;
            memReq->time = curTick;
            dcacheInterface->access(memReq);
        }
    }
    return fault;
#endif
}

#if FULL_SYSTEM
template <class Impl>
Addr
OzoneCPU<Impl>::dbg_vtophys(Addr addr)
{
    return vtophys(tc, addr);
}
#endif // FULL_SYSTEM

#if FULL_SYSTEM
template <class Impl>
void
OzoneCPU<Impl>::post_interrupt(int int_num, int index)
{
    BaseCPU::post_interrupt(int_num, index);

    if (_status == Idle) {
        DPRINTF(IPI,"Suspended Processor awoke\n");
//	thread.activate();
        // Hack for now.  Otherwise might have to go through the tc, or
        // I need to figure out what's the right thing to call.
        activateContext(thread.readTid(), 1);
    }
}
#endif // FULL_SYSTEM

/* start simulation, program loaded, processor precise state initialized */
template <class Impl>
void
OzoneCPU<Impl>::tick()
{
    DPRINTF(OzoneCPU, "\n\nOzoneCPU: Ticking cpu.\n");

    _status = Running;
    thread.renameTable[ZeroReg]->setIntResult(0);
    thread.renameTable[ZeroReg+TheISA::FP_Base_DepTag]->
        setDoubleResult(0.0);

    comm.advance();
    frontEnd->tick();
    backEnd->tick();

    // check for instruction-count-based events
    comInstEventQueue[0]->serviceEvents(numInst);

    if (!tickEvent.scheduled() && _status == Running)
        tickEvent.schedule(curTick + cycles(1));
}

template <class Impl>
void
OzoneCPU<Impl>::squashFromTC()
{
    thread.inSyscall = true;
    backEnd->generateTCEvent();
}

#if !FULL_SYSTEM
template <class Impl>
void
OzoneCPU<Impl>::syscall(uint64_t &callnum)
{
    // Not sure this copy is needed, depending on how the TC proxy is made.
    thread.renameTable.copyFrom(backEnd->renameTable);

    thread.inSyscall = true;

    thread.funcExeInst++;

    DPRINTF(OzoneCPU, "FuncExeInst: %i\n", thread.funcExeInst);

    thread.process->syscall(callnum, tc);

    thread.funcExeInst--;

    thread.inSyscall = false;

    frontEnd->renameTable.copyFrom(thread.renameTable);
    backEnd->renameTable.copyFrom(thread.renameTable);
}

template <class Impl>
void
OzoneCPU<Impl>::setSyscallReturn(SyscallReturn return_value, int tid)
{
    // check for error condition.  Alpha syscall convention is to
    // indicate success/failure in reg a3 (r19) and put the
    // return value itself in the standard return value reg (v0).
    if (return_value.successful()) {
        // no error
        thread.renameTable[SyscallSuccessReg]->setIntResult(0);
        thread.renameTable[ReturnValueReg]->setIntResult(
            return_value.value());
    } else {
        // got an error, return details
        thread.renameTable[SyscallSuccessReg]->setIntResult((IntReg) -1);
        thread.renameTable[ReturnValueReg]->setIntResult(
            -return_value.value());
    }
}
#else
template <class Impl>
Fault
OzoneCPU<Impl>::hwrei()
{
    // Need to move this to ISA code
    // May also need to make this per thread

    lockFlag = false;
    lockAddrList.clear();
    thread.kernelStats->hwrei();

    checkInterrupts = true;

    // FIXME: XXX check for interrupts? XXX
    return NoFault;
}

template <class Impl>
void
OzoneCPU<Impl>::processInterrupts()
{
    // Check for interrupts here.  For now can copy the code that
    // exists within isa_fullsys_traits.hh.  Also assume that thread 0
    // is the one that handles the interrupts.

    // Check if there are any outstanding interrupts
    //Handle the interrupts
    int ipl = 0;
    int summary = 0;

    checkInterrupts = false;

    if (thread.readMiscReg(IPR_ASTRR))
        panic("asynchronous traps not implemented\n");

    if (thread.readMiscReg(IPR_SIRR)) {
        for (int i = INTLEVEL_SOFTWARE_MIN;
             i < INTLEVEL_SOFTWARE_MAX; i++) {
            if (thread.readMiscReg(IPR_SIRR) & (ULL(1) << i)) {
                // See table 4-19 of the 21164 hardware reference
                ipl = (i - INTLEVEL_SOFTWARE_MIN) + 1;
                summary |= (ULL(1) << i);
            }
        }
    }

    uint64_t interrupts = intr_status();

    if (interrupts) {
        for (int i = INTLEVEL_EXTERNAL_MIN;
             i < INTLEVEL_EXTERNAL_MAX; i++) {
            if (interrupts & (ULL(1) << i)) {
                // See table 4-19 of the 21164 hardware reference
                ipl = i;
                summary |= (ULL(1) << i);
            }
        }
    }

    if (ipl && ipl > thread.readMiscReg(IPR_IPLR)) {
        thread.setMiscReg(IPR_ISR, summary);
        thread.setMiscReg(IPR_INTID, ipl);
        // @todo: Make this more transparent
        if (checker) {
            checker->threadBase()->setMiscReg(IPR_ISR, summary);
            checker->threadBase()->setMiscReg(IPR_INTID, ipl);
        }
        Fault fault = new InterruptFault;
        fault->invoke(thread.getTC());
        DPRINTF(Flow, "Interrupt! IPLR=%d ipl=%d summary=%x\n",
                thread.readMiscReg(IPR_IPLR), ipl, summary);
    }
}

template <class Impl>
bool
OzoneCPU<Impl>::simPalCheck(int palFunc)
{
    // Need to move this to ISA code
    // May also need to make this per thread
    thread.kernelStats->callpal(palFunc, tc);

    switch (palFunc) {
      case PAL::halt:
        haltContext(thread.readTid());
        if (--System::numSystemsRunning == 0)
            exitSimLoop("all cpus halted");
        break;

      case PAL::bpt:
      case PAL::bugchk:
        if (system->breakpoint())
            return false;
        break;
    }

    return true;
}
#endif

template <class Impl>
BaseCPU *
OzoneCPU<Impl>::OzoneTC::getCpuPtr()
{
    return cpu;
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setCpuId(int id)
{
    cpu->cpuId = id;
    thread->setCpuId(id);
}

#if FULL_SYSTEM
template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::delVirtPort(VirtualPort *vp)
{
    delete vp->getPeer();
    delete vp;
}
#endif

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setStatus(Status new_status)
{
    thread->setStatus(new_status);
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::activate(int delay)
{
    cpu->activateContext(thread->readTid(), delay);
}

/// Set the status to Suspended.
template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::suspend()
{
    cpu->suspendContext(thread->readTid());
}

/// Set the status to Unallocated.
template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::deallocate(int delay)
{
    cpu->deallocateContext(thread->readTid(), delay);
}

/// Set the status to Halted.
template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::halt()
{
    cpu->haltContext(thread->readTid());
}

#if FULL_SYSTEM
template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::dumpFuncProfile()
{ }
#endif

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::takeOverFrom(ThreadContext *old_context)
{
    // some things should already be set up
#if FULL_SYSTEM
    assert(getSystemPtr() == old_context->getSystemPtr());
#else
    assert(getProcessPtr() == old_context->getProcessPtr());
#endif

    // copy over functional state
    setStatus(old_context->status());
    copyArchRegs(old_context);
    setCpuId(old_context->readCpuId());

#if !FULL_SYSTEM
    setFuncExeInst(old_context->readFuncExeInst());
#else
    EndQuiesceEvent *other_quiesce = old_context->getQuiesceEvent();
    if (other_quiesce) {
        // Point the quiesce event's TC at this TC so that it wakes up
        // the proper CPU.
        other_quiesce->tc = this;
    }
    if (thread->quiesceEvent) {
        thread->quiesceEvent->tc = this;
    }

    thread->kernelStats = old_context->getKernelStats();
//    storeCondFailures = 0;
    cpu->lockFlag = false;
#endif

    old_context->setStatus(ThreadContext::Unallocated);
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::regStats(const std::string &name)
{
#if FULL_SYSTEM
    thread->kernelStats = new Kernel::Statistics(cpu->system);
    thread->kernelStats->regStats(name + ".kern");
#endif
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::serialize(std::ostream &os)
{ }

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::unserialize(Checkpoint *cp, const std::string &section)
{ }

#if FULL_SYSTEM
template <class Impl>
EndQuiesceEvent *
OzoneCPU<Impl>::OzoneTC::getQuiesceEvent()
{
    return thread->quiesceEvent;
}

template <class Impl>
Tick
OzoneCPU<Impl>::OzoneTC::readLastActivate()
{
    return thread->lastActivate;
}

template <class Impl>
Tick
OzoneCPU<Impl>::OzoneTC::readLastSuspend()
{
    return thread->lastSuspend;
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::profileClear()
{
    if (thread->profile)
        thread->profile->clear();
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::profileSample()
{
    if (thread->profile)
        thread->profile->sample(thread->profileNode, thread->profilePC);
}
#endif

template <class Impl>
int
OzoneCPU<Impl>::OzoneTC::getThreadNum()
{
    return thread->readTid();
}

// Also somewhat obnoxious.  Really only used for the TLB fault.
template <class Impl>
TheISA::MachInst
OzoneCPU<Impl>::OzoneTC::getInst()
{
    return thread->getInst();
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::copyArchRegs(ThreadContext *tc)
{
    thread->PC = tc->readPC();
    thread->nextPC = tc->readNextPC();

    cpu->frontEnd->setPC(thread->PC);
    cpu->frontEnd->setNextPC(thread->nextPC);

    for (int i = 0; i < TheISA::TotalNumRegs; ++i) {
        if (i < TheISA::FP_Base_DepTag) {
            thread->renameTable[i]->setIntResult(tc->readIntReg(i));
        } else if (i < (TheISA::FP_Base_DepTag + TheISA::NumFloatRegs)) {
            int fp_idx = i - TheISA::FP_Base_DepTag;
            thread->renameTable[i]->setDoubleResult(
                tc->readFloatReg(fp_idx, 64));
        }
    }

#if !FULL_SYSTEM
    thread->funcExeInst = tc->readFuncExeInst();
#endif

    // Need to copy the TC values into the current rename table,
    // copy the misc regs.
    copyMiscRegs(tc, this);
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::clearArchRegs()
{
    panic("Unimplemented!");
}

template <class Impl>
uint64_t
OzoneCPU<Impl>::OzoneTC::readIntReg(int reg_idx)
{
    return thread->renameTable[reg_idx]->readIntResult();
}

template <class Impl>
TheISA::FloatReg
OzoneCPU<Impl>::OzoneTC::readFloatReg(int reg_idx, int width)
{
    int idx = reg_idx + TheISA::FP_Base_DepTag;
    switch(width) {
      case 32:
        return thread->renameTable[idx]->readFloatResult();
      case 64:
        return thread->renameTable[idx]->readDoubleResult();
      default:
        panic("Unsupported width!");
        return 0;
    }
}

template <class Impl>
double
OzoneCPU<Impl>::OzoneTC::readFloatReg(int reg_idx)
{
    int idx = reg_idx + TheISA::FP_Base_DepTag;
    return thread->renameTable[idx]->readFloatResult();
}

template <class Impl>
uint64_t
OzoneCPU<Impl>::OzoneTC::readFloatRegBits(int reg_idx, int width)
{
    int idx = reg_idx + TheISA::FP_Base_DepTag;
    return thread->renameTable[idx]->readIntResult();
}

template <class Impl>
uint64_t
OzoneCPU<Impl>::OzoneTC::readFloatRegBits(int reg_idx)
{
    int idx = reg_idx + TheISA::FP_Base_DepTag;
    return thread->renameTable[idx]->readIntResult();
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setIntReg(int reg_idx, uint64_t val)
{
    thread->renameTable[reg_idx]->setIntResult(val);

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setFloatReg(int reg_idx, FloatReg val, int width)
{
    int idx = reg_idx + TheISA::FP_Base_DepTag;
    switch(width) {
      case 32:
        panic("Unimplemented!");
        break;
      case 64:
        thread->renameTable[idx]->setDoubleResult(val);
        break;
      default:
        panic("Unsupported width!");
    }

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setFloatReg(int reg_idx, FloatReg val)
{
    int idx = reg_idx + TheISA::FP_Base_DepTag;

    thread->renameTable[idx]->setDoubleResult(val);

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setFloatRegBits(int reg_idx, FloatRegBits val,
                                         int width)
{
    panic("Unimplemented!");
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setFloatRegBits(int reg_idx, FloatRegBits val)
{
    panic("Unimplemented!");
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setPC(Addr val)
{
    thread->PC = val;
    cpu->frontEnd->setPC(val);

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }
}

template <class Impl>
void
OzoneCPU<Impl>::OzoneTC::setNextPC(Addr val)
{
    thread->nextPC = val;
    cpu->frontEnd->setNextPC(val);

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }
}

template <class Impl>
TheISA::MiscReg
OzoneCPU<Impl>::OzoneTC::readMiscReg(int misc_reg)
{
    return thread->miscRegFile.readReg(misc_reg);
}

template <class Impl>
TheISA::MiscReg
OzoneCPU<Impl>::OzoneTC::readMiscRegWithEffect(int misc_reg, Fault &fault)
{
    return thread->miscRegFile.readRegWithEffect(misc_reg,
                                                 fault, this);
}

template <class Impl>
Fault
OzoneCPU<Impl>::OzoneTC::setMiscReg(int misc_reg, const MiscReg &val)
{
    // Needs to setup a squash event unless we're in syscall mode
    Fault ret_fault = thread->miscRegFile.setReg(misc_reg, val);

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }

    return ret_fault;
}

template <class Impl>
Fault
OzoneCPU<Impl>::OzoneTC::setMiscRegWithEffect(int misc_reg, const MiscReg &val)
{
    // Needs to setup a squash event unless we're in syscall mode
    Fault ret_fault = thread->miscRegFile.setRegWithEffect(misc_reg, val,
                                                           this);

    if (!thread->inSyscall) {
        cpu->squashFromTC();
    }

    return ret_fault;
}
