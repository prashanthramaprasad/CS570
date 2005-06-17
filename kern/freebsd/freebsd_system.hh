/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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
 */

#ifndef __KERN_FREEBSD_FREEBSD_SYSTEM_HH__
#define __KERN_FREEBSD_FREEBSD_SYSTEM_HH__

#include "sim/host.hh"
#include "sim/system.hh"
#include "targetarch/isa_traits.hh"
#include "kern/freebsd/freebsd_events.hh"


/**
 * This class skips lengthy functions in the FreeBSD kernel.
 */
class FreebsdSystem : public System
{
  private:

    /** PC based event to skip the DELAY call */
    SkipFuncEvent *skipDelayEvent;

    SkipFuncEvent *skipOROMEvent;

    SkipFuncEvent *skipAicEvent;

    SkipFuncEvent *skipPNPEvent;

    SkipFuncEvent *skipATAEvent;

    FreebsdSkipCalibrateClocksEvent *skipCalibrateClocks;

  public:
    FreebsdSystem(Params *p);
    ~FreebsdSystem();
    void doCalibrateClocks(ExecContext *xc);

};

#endif // __KERN_FREEBSD_FREEBSD_SYSTEM_HH__
