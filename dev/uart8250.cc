/*
 * Copyright (c) 2005 The Regents of The University of Michigan
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

/** @file
 * Implements a 8250 UART
 */

#include <string>
#include <vector>

#include "arch/alpha/ev5.hh"
#include "base/inifile.hh"
#include "base/str.hh"        // for to_number
#include "base/trace.hh"
#include "dev/simconsole.hh"
#include "dev/uart8250.hh"
#include "dev/platform.hh"
#include "sim/builder.hh"

using namespace std;
using namespace TheISA;

Uart8250::IntrEvent::IntrEvent(Uart8250 *u, int bit)
    : Event(&mainEventQueue), uart(u)
{
    DPRINTF(Uart, "UART Interrupt Event Initilizing\n");
    intrBit = bit;
}

const char *
Uart8250::IntrEvent::description()
{
    return "uart interrupt delay event";
}

void
Uart8250::IntrEvent::process()
{
    if (intrBit & uart->IER) {
       DPRINTF(Uart, "UART InterEvent, interrupting\n");
       uart->platform->postConsoleInt();
       uart->status |= intrBit;
    }
    else
       DPRINTF(Uart, "UART InterEvent, not interrupting\n");

}

/* The linux serial driver (8250.c about line 1182) loops reading from
 * the device until the device reports it has no more data to
 * read. After a maximum of 255 iterations the code prints "serial8250
 * too much work for irq X," and breaks out of the loop. Since the
 * simulated system is so much slower than the actual system, if a
 * user is typing on the keyboard it is very easy for them to provide
 * input at a fast enough rate to not allow the loop to exit and thus
 * the error to be printed. This magic number provides a delay between
 * the time the UART receives a character to send to the simulated
 * system and the time it actually notifies the system it has a
 * character to send to alleviate this problem. --Ali
 */
void
Uart8250::IntrEvent::scheduleIntr()
{
    static const Tick interval = (Tick)((Clock::Float::s / 2e9) * 450);
    DPRINTF(Uart, "Scheduling IER interrupt for %#x, at cycle %lld\n", intrBit,
            curTick + interval);
    if (!scheduled())
        schedule(curTick + interval);
    else
        reschedule(curTick + interval);
}


Uart8250::Uart8250(Params *p)
    : Uart(p), txIntrEvent(this, TX_INT), rxIntrEvent(this, RX_INT)
{
    pioSize = 8;

    IER = 0;
    DLAB = 0;
    LCR = 0;
    MCR = 0;
}

Tick
Uart8250::read(Packet &pkt)
{
    assert(pkt.result == Unknown);
    assert(pkt.addr >= pioAddr && pkt.addr < pioAddr + pioSize);
    assert(pkt.size == 1);

    pkt.time = curTick + pioDelay;
    Addr daddr = pkt.addr - pioAddr;
    uint8_t *data;

    DPRINTF(Uart, " read register %#x\n", daddr);

    if (!pkt.data) {
        data = new uint8_t;
        pkt.data = data;
    } else
        data = pkt.data;

    switch (daddr) {
        case 0x0:
            if (!(LCR & 0x80)) { // read byte
                if (cons->dataAvailable())
                    cons->in(*data);
                else {
                    *data = 0;
                    // A limited amount of these are ok.
                    DPRINTF(Uart, "empty read of RX register\n");
                }
                status &= ~RX_INT;
                platform->clearConsoleInt();

                if (cons->dataAvailable() && (IER & UART_IER_RDI))
                    rxIntrEvent.scheduleIntr();
            } else { // dll divisor latch
               ;
            }
            break;
        case 0x1:
            if (!(LCR & 0x80)) { // Intr Enable Register(IER)
                *data = IER;
            } else { // DLM divisor latch MSB
                ;
            }
            break;
        case 0x2: // Intr Identification Register (IIR)
            DPRINTF(Uart, "IIR Read, status = %#x\n", (uint32_t)status);

            if (status & RX_INT) /* Rx data interrupt has a higher priority */
                *data = IIR_RXID;
            else if (status & TX_INT)
                *data = IIR_TXID;
            else
                *data = IIR_NOPEND;

            //Tx interrupts are cleared on IIR reads
            status &= ~TX_INT;
            break;
        case 0x3: // Line Control Register (LCR)
            *data = LCR;
            break;
        case 0x4: // Modem Control Register (MCR)
            break;
        case 0x5: // Line Status Register (LSR)
            uint8_t lsr;
            lsr = 0;
            // check if there are any bytes to be read
            if (cons->dataAvailable())
                lsr = UART_LSR_DR;
            lsr |= UART_LSR_TEMT | UART_LSR_THRE;
            *data = lsr;
            break;
        case 0x6: // Modem Status Register (MSR)
            *data = 0;
            break;
        case 0x7: // Scratch Register (SCR)
            *data = 0; // doesn't exist with at 8250.
            break;
        default:
            panic("Tried to access a UART port that doesn't exist\n");
            break;
    }
/*    uint32_t d32 = *data;
    DPRINTF(Uart, "Register read to register %#x returned %#x\n", daddr, d32);
*/
    pkt.result = Success;
    return pioDelay;
}

Tick
Uart8250::write(Packet &pkt)
{

    assert(pkt.result == Unknown);
    assert(pkt.addr >= pioAddr && pkt.addr < pioAddr + pioSize);
    assert(pkt.size == 1);

    pkt.time = curTick + pioDelay;
    Addr daddr = pkt.addr - pioAddr;

    uint8_t *data = pkt.data;

    DPRINTF(Uart, " write register %#x value %#x\n", daddr, *data);

    switch (daddr) {
        case 0x0:
            if (!(LCR & 0x80)) { // write byte
                cons->out(*data);
                platform->clearConsoleInt();
                status &= ~TX_INT;
                if (UART_IER_THRI & IER)
                    txIntrEvent.scheduleIntr();
            } else { // dll divisor latch
               ;
            }
            break;
        case 0x1:
            if (!(LCR & 0x80)) { // Intr Enable Register(IER)
                IER = *data;
                if (UART_IER_THRI & IER)
                {
                    DPRINTF(Uart, "IER: IER_THRI set, scheduling TX intrrupt\n");
                    txIntrEvent.scheduleIntr();
                }
                else
                {
                    DPRINTF(Uart, "IER: IER_THRI cleared, descheduling TX intrrupt\n");
                    if (txIntrEvent.scheduled())
                        txIntrEvent.deschedule();
                    if (status & TX_INT)
                        platform->clearConsoleInt();
                    status &= ~TX_INT;
                }

                if ((UART_IER_RDI & IER) && cons->dataAvailable()) {
                    DPRINTF(Uart, "IER: IER_RDI set, scheduling RX intrrupt\n");
                    rxIntrEvent.scheduleIntr();
                } else {
                    DPRINTF(Uart, "IER: IER_RDI cleared, descheduling RX intrrupt\n");
                    if (rxIntrEvent.scheduled())
                        rxIntrEvent.deschedule();
                    if (status & RX_INT)
                        platform->clearConsoleInt();
                    status &= ~RX_INT;
                }
             } else { // DLM divisor latch MSB
                ;
            }
            break;
        case 0x2: // FIFO Control Register (FCR)
            break;
        case 0x3: // Line Control Register (LCR)
            LCR = *data;
            break;
        case 0x4: // Modem Control Register (MCR)
            if (*data == (UART_MCR_LOOP | 0x0A))
                    MCR = 0x9A;
            break;
        case 0x7: // Scratch Register (SCR)
            // We are emulating a 8250 so we don't have a scratch reg
            break;
        default:
            panic("Tried to access a UART port that doesn't exist\n");
            break;
    }
    pkt.result = Success;
    return pioDelay;
}

void
Uart8250::dataAvailable()
{
    // if the kernel wants an interrupt when we have data
    if (IER & UART_IER_RDI)
    {
        platform->postConsoleInt();
        status |= RX_INT;
    }

}

void
Uart8250::addressRanges(AddrRangeList &range_list)
{
    assert(pioSize != 0);
    range_list.clear();
    range_list.push_back(RangeSize(pioAddr, pioSize));
}



void
Uart8250::serialize(ostream &os)
{
    SERIALIZE_SCALAR(status);
    SERIALIZE_SCALAR(IER);
    SERIALIZE_SCALAR(DLAB);
    SERIALIZE_SCALAR(LCR);
    SERIALIZE_SCALAR(MCR);
    Tick rxintrwhen;
    if (rxIntrEvent.scheduled())
        rxintrwhen = rxIntrEvent.when();
    else
        rxintrwhen = 0;
    Tick txintrwhen;
    if (txIntrEvent.scheduled())
        txintrwhen = txIntrEvent.when();
    else
        txintrwhen = 0;
     SERIALIZE_SCALAR(rxintrwhen);
     SERIALIZE_SCALAR(txintrwhen);
}

void
Uart8250::unserialize(Checkpoint *cp, const std::string &section)
{
    UNSERIALIZE_SCALAR(status);
    UNSERIALIZE_SCALAR(IER);
    UNSERIALIZE_SCALAR(DLAB);
    UNSERIALIZE_SCALAR(LCR);
    UNSERIALIZE_SCALAR(MCR);
    Tick rxintrwhen;
    Tick txintrwhen;
    UNSERIALIZE_SCALAR(rxintrwhen);
    UNSERIALIZE_SCALAR(txintrwhen);
    if (rxintrwhen != 0)
        rxIntrEvent.schedule(rxintrwhen);
    if (txintrwhen != 0)
        txIntrEvent.schedule(txintrwhen);
}

BEGIN_DECLARE_SIM_OBJECT_PARAMS(Uart8250)

    Param<Addr> pio_addr;
    Param<Tick> pio_latency;
    SimObjectParam<Platform *> platform;
    SimObjectParam<SimConsole *> sim_console;
    SimObjectParam<System *> system;

END_DECLARE_SIM_OBJECT_PARAMS(Uart8250)

BEGIN_INIT_SIM_OBJECT_PARAMS(Uart8250)

    INIT_PARAM(pio_addr, "Device Address"),
    INIT_PARAM_DFLT(pio_latency, "Programmed IO latency", 1000),
    INIT_PARAM(platform, "platform"),
    INIT_PARAM(sim_console, "The Simulator Console"),
    INIT_PARAM(system, "system object")

END_INIT_SIM_OBJECT_PARAMS(Uart8250)

CREATE_SIM_OBJECT(Uart8250)
{
    Uart8250::Params *p = new Uart8250::Params;
    p->name = getInstanceName();
    p->pio_addr = pio_addr;
    p->pio_delay = pio_latency;
    p->platform = platform;
    p->cons = sim_console;
    p->system = system;
    return new Uart8250(p);
}

REGISTER_SIM_OBJECT("Uart8250", Uart8250)

