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
 *
 * Authors: Ali Saidi
 */

/**
 * @file
 * Declaration of top level class for the Tsunami chipset. This class just
 * retains pointers to all its children so the children can communicate.
 */

#ifndef __DEV_T1000_HH__
#define __DEV_T1000_HH__

#include "dev/platform.hh"

class IdeController;
class System;

class T1000 : public Platform
{
  public:
    /** Pointer to the system */
    System *system;

  public:
    /**
     * Constructor for the Tsunami Class.
     * @param name name of the object
     * @param s system the object belongs to
     * @param intctrl pointer to the interrupt controller
     */
    T1000(const std::string &name, System *s, IntrControl *intctrl);

    /**
     * Return the interrupting frequency to AlphaAccess
     * @return frequency of RTC interrupts
     */
    virtual Tick intrFrequency();

    /**
     * Cause the cpu to post a serial interrupt to the CPU.
     */
    virtual void postConsoleInt();

    /**
     * Clear a posted CPU interrupt (id=55)
     */
    virtual void clearConsoleInt();

    /**
     * Cause the chipset to post a cpi interrupt to the CPU.
     */
    virtual void postPciInt(int line);

    /**
     * Clear a posted PCI->CPU interrupt
     */
    virtual void clearPciInt(int line);


    virtual Addr pciToDma(Addr pciAddr) const;

    /**
     * Calculate the configuration address given a bus/dev/func.
     */
    virtual Addr calcConfigAddr(int bus, int dev, int func);

    /**
     * Serialize this object to the given output stream.
     * @param os The stream to serialize to.
     */
    virtual void serialize(std::ostream &os);

    /**
     * Reconstruct the state of this object from a checkpoint.
     * @param cp The checkpoint use.
     * @param section The section name of this object
     */
    virtual void unserialize(Checkpoint *cp, const std::string &section);
};

#endif // __DEV_T1000_HH__
