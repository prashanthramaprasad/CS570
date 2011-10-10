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
 *          Andrew Schultz
 *          Miguel Serrano
 */

#include <sys/time.h>

#include <ctime>
#include <string>

#include "base/bitfield.hh"
#include "base/time.hh"
#include "base/trace.hh"
#include "debug/MC146818.hh"
#include "dev/mc146818.hh"
#include "dev/rtcreg.h"

using namespace std;

static uint8_t
bcdize(uint8_t val)
{
    uint8_t result;
    result = val % 10;
    result += (val / 10) << 4;
    return result;
}

static uint8_t
unbcdize(uint8_t val)
{
    uint8_t result;
    result = val & 0xf;
    result += (val >> 4) * 10;
    return result;
}

void
MC146818::setTime(const struct tm time)
{
    curTime = time;
    year = time.tm_year;
    // Unix is 0-11 for month, data seet says start at 1
    mon = time.tm_mon + 1;
    mday = time.tm_mday;
    hour = time.tm_hour;
    min = time.tm_min;
    sec = time.tm_sec;

    // Datasheet says 1 is sunday
    wday = time.tm_wday + 1;

    if (!(stat_regB & RTCB_BIN)) {
        // The datasheet says that the year field can be either BCD or
        // years since 1900.  Linux seems to be happy with years since
        // 1900.
        year = bcdize(year % 100);
        mon = bcdize(mon);
        mday = bcdize(mday);
        hour = bcdize(hour);
        min = bcdize(min);
        sec = bcdize(sec);
    }
}

MC146818::MC146818(EventManager *em, const string &n, const struct tm time,
                   bool bcd, Tick frequency)
    : EventManager(em), _name(n), event(this, frequency), tickEvent(this)
{
    memset(clock_data, 0, sizeof(clock_data));
    stat_regA = RTCA_32768HZ | RTCA_1024HZ;
    stat_regB = RTCB_PRDC_IE | RTCB_24HR;
    if (!bcd)
        stat_regB |= RTCB_BIN;

    setTime(time);
    DPRINTFN("Real-time clock set to %s", asctime(&time));
}

MC146818::~MC146818()
{
    deschedule(tickEvent);
    deschedule(event);
}

void
MC146818::writeData(const uint8_t addr, const uint8_t data)
{
    if (addr < RTC_STAT_REGA) {
        clock_data[addr] = data;
        curTime.tm_sec = unbcdize(sec);
        curTime.tm_min = unbcdize(min);
        curTime.tm_hour = unbcdize(hour);
        curTime.tm_mday = unbcdize(mday);
        curTime.tm_mon = unbcdize(mon) - 1;
        curTime.tm_year = ((unbcdize(year) + 50) % 100) + 1950;
        curTime.tm_wday = unbcdize(wday) - 1;
    } else {
        switch (addr) {
          case RTC_STAT_REGA:
            // The "update in progress" bit is read only.
            if ((data & ~RTCA_UIP) != (RTCA_32768HZ | RTCA_1024HZ))
                panic("Unimplemented RTC register A value write!\n");
            replaceBits(stat_regA, data, 6, 0);
            break;
          case RTC_STAT_REGB:
            if ((data & ~(RTCB_PRDC_IE | RTCB_SQWE)) != RTCB_24HR)
                panic("Write to RTC reg B bits that are not implemented!\n");

            if (data & RTCB_PRDC_IE) {
                if (!event.scheduled())
                    event.scheduleIntr();
            } else {
                if (event.scheduled())
                    deschedule(event);
            }
            stat_regB = data;
            break;
          case RTC_STAT_REGC:
          case RTC_STAT_REGD:
            panic("RTC status registers C and D are not implemented.\n");
            break;
        }
    }
}

uint8_t
MC146818::readData(uint8_t addr)
{
    if (addr < RTC_STAT_REGA)
        return clock_data[addr];
    else {
        switch (addr) {
          case RTC_STAT_REGA:
            // toggle UIP bit for linux
            stat_regA ^= RTCA_UIP;
            return stat_regA;
            break;
          case RTC_STAT_REGB:
            return stat_regB;
            break;
          case RTC_STAT_REGC:
          case RTC_STAT_REGD:
            return 0x00;
            break;
          default:
            panic("Shouldn't be here");
        }
    }
}

static time_t
mkutctime(struct tm *time)
{
    time_t ret;
    char *tz;

    tz = getenv("TZ");
    setenv("TZ", "", 1);
    tzset();
    ret = mktime(time);
    if (tz)
        setenv("TZ", tz, 1);
    else
        unsetenv("TZ");
    tzset();
    return ret;
}

void
MC146818::tickClock()
{
    if (stat_regB & RTCB_NO_UPDT)
        return;
    time_t calTime = mkutctime(&curTime);
    calTime++;
    setTime(*gmtime(&calTime));
}

void
MC146818::serialize(const string &base, ostream &os)
{
    arrayParamOut(os, base + ".clock_data", clock_data, sizeof(clock_data));
    paramOut(os, base + ".stat_regA", stat_regA);
    paramOut(os, base + ".stat_regB", stat_regB);

    //
    // save the timer tick and rtc clock tick values to correctly reschedule 
    // them during unserialize
    //
    Tick rtcTimerInterruptTickOffset = event.when() - curTick();
    SERIALIZE_SCALAR(rtcTimerInterruptTickOffset);
    Tick rtcClockTickOffset = tickEvent.when() - curTick();
    SERIALIZE_SCALAR(rtcClockTickOffset);
}

void
MC146818::unserialize(const string &base, Checkpoint *cp,
                      const string &section)
{
    arrayParamIn(cp, section, base + ".clock_data", clock_data,
                 sizeof(clock_data));
    paramIn(cp, section, base + ".stat_regA", stat_regA);
    paramIn(cp, section, base + ".stat_regB", stat_regB);

    //
    // properly schedule the timer and rtc clock events
    //
    Tick rtcTimerInterruptTickOffset;
    UNSERIALIZE_SCALAR(rtcTimerInterruptTickOffset);
    reschedule(event, curTick() + rtcTimerInterruptTickOffset);
    Tick rtcClockTickOffset;
    UNSERIALIZE_SCALAR(rtcClockTickOffset);
    reschedule(tickEvent, curTick() + rtcClockTickOffset);
}

MC146818::RTCEvent::RTCEvent(MC146818 * _parent, Tick i)
    : parent(_parent), interval(i)
{
    DPRINTF(MC146818, "RTC Event Initilizing\n");
    parent->schedule(this, curTick() + interval);
}

void
MC146818::RTCEvent::scheduleIntr()
{
    parent->schedule(this, curTick() + interval);
}

void
MC146818::RTCEvent::process()
{
    DPRINTF(MC146818, "RTC Timer Interrupt\n");
    parent->schedule(this, curTick() + interval);
    parent->handleEvent();
}

const char *
MC146818::RTCEvent::description() const
{
    return "RTC interrupt";
}

void
MC146818::RTCTickEvent::process()
{
    DPRINTF(MC146818, "RTC clock tick\n");
    parent->schedule(this, curTick() + SimClock::Int::s);
    parent->tickClock();
}

const char *
MC146818::RTCTickEvent::description() const
{
    return "RTC clock tick";
}
