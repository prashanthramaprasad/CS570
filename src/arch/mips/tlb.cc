/*
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
 * Copyright (c) 2007 MIPS Technologies, Inc.
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
 * Authors: Nathan Binkert
 *          Steve Reinhardt
 *          Jaidev Patwardhan
 */

#include <string>
#include <vector>

#include "arch/mips/pra_constants.hh"
#include "arch/mips/pagetable.hh"
#include "arch/mips/tlb.hh"
#include "arch/mips/faults.hh"
#include "arch/mips/utility.hh"
#include "base/inifile.hh"
#include "base/str.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "sim/process.hh"
#include "mem/page_table.hh"
#include "params/MipsDTB.hh"
#include "params/MipsITB.hh"
#include "params/MipsTLB.hh"
#include "params/MipsUTB.hh"


using namespace std;
using namespace MipsISA;

///////////////////////////////////////////////////////////////////////
//
//  MIPS TLB
//

#define MODE2MASK(X)                    (1 << (X))

TLB::TLB(const Params *p)
    : BaseTLB(p), size(p->size), nlu(0)
{
    table = new MipsISA::PTE[size];
    memset(table, 0, sizeof(MipsISA::PTE[size]));
    smallPages=0;
}

TLB::~TLB()
{
    if (table)
        delete [] table;
}

// look up an entry in the TLB
MipsISA::PTE *
TLB::lookup(Addr vpn, uint8_t asn) const
{
    // assume not found...
    MipsISA::PTE *retval = NULL;
    PageTable::const_iterator i = lookupTable.find(vpn);
    if (i != lookupTable.end()) {
        while (i->first == vpn) {
            int index = i->second;
            MipsISA::PTE *pte = &table[index];

            /* 1KB TLB Lookup code - from MIPS ARM Volume III - Rev. 2.50 */
            Addr Mask = pte->Mask;
            Addr InvMask = ~Mask;
            Addr VPN  = pte->VPN;
            //      warn("Valid: %d - %d\n",pte->V0,pte->V1);
            if(((vpn & InvMask) == (VPN & InvMask)) && (pte->G  || (asn == pte->asid)))
              { // We have a VPN + ASID Match
                retval = pte;
                break;
              }
            ++i;
        }
    }

    DPRINTF(TLB, "lookup %#x, asn %#x -> %s ppn %#x\n", vpn, (int)asn,
            retval ? "hit" : "miss", retval ? retval->PFN1 : 0);
    return retval;
}

MipsISA::PTE* TLB::getEntry(unsigned Index) const
{
    // Make sure that Index is valid
    assert(Index<size);
    return &table[Index];
}

int TLB::probeEntry(Addr vpn,uint8_t asn) const
{
    // assume not found...
    MipsISA::PTE *retval = NULL;
    int Ind=-1;
    PageTable::const_iterator i = lookupTable.find(vpn);
    if (i != lookupTable.end()) {
        while (i->first == vpn) {
            int index = i->second;
            MipsISA::PTE *pte = &table[index];

            /* 1KB TLB Lookup code - from MIPS ARM Volume III - Rev. 2.50 */
            Addr Mask = pte->Mask;
            Addr InvMask = ~Mask;
            Addr VPN  = pte->VPN;
            if(((vpn & InvMask) == (VPN & InvMask)) && (pte->G  || (asn == pte->asid)))
              { // We have a VPN + ASID Match
                retval = pte;
                Ind = index;
                break;
              }

            ++i;
        }
    }
    DPRINTF(MipsPRA,"VPN: %x, asid: %d, Result of TLBP: %d\n",vpn,asn,Ind);
    return Ind;
}
Fault inline
TLB::checkCacheability(RequestPtr &req)
{
  Addr VAddrUncacheable = 0xA0000000;
  // In MIPS, cacheability is controlled by certain bits of the virtual address
  // or by the TLB entry
  if((req->getVaddr() & VAddrUncacheable) == VAddrUncacheable) {
    // mark request as uncacheable
    req->setFlags(req->getFlags() | UNCACHEABLE);
  }
  return NoFault;
}
void TLB::insertAt(MipsISA::PTE &pte, unsigned Index, int _smallPages)
{
  smallPages=_smallPages;
  if(Index > size){
    warn("Attempted to write at index (%d) beyond TLB size (%d)",Index,size);
  } else {
    // Update TLB
    DPRINTF(TLB,"TLB[%d]: %x %x %x %x\n",Index,pte.Mask<<11,((pte.VPN << 11) | pte.asid),((pte.PFN0 <<6) | (pte.C0 << 3) | (pte.D0 << 2) | (pte.V0 <<1) | pte.G),
            ((pte.PFN1 <<6) | (pte.C1 << 3) | (pte.D1 << 2) | (pte.V1 <<1) | pte.G));
    if(table[Index].V0 == true || table[Index].V1 == true){ // Previous entry is valid
      PageTable::iterator i = lookupTable.find(table[Index].VPN);
      lookupTable.erase(i);
    }
    table[Index]=pte;
    // Update fast lookup table
    lookupTable.insert(make_pair(table[Index].VPN, Index));
    //    int TestIndex=probeEntry(pte.VPN,pte.asid);
    //    warn("Inserted at: %d, Found at: %d (%x)\n",Index,TestIndex,pte.Mask);
  }

}

// insert a new TLB entry
void
TLB::insert(Addr addr, MipsISA::PTE &pte)
{
  fatal("TLB Insert not yet implemented\n");


  /*    MipsISA::VAddr vaddr = addr;
    if (table[nlu].valid) {
        Addr oldvpn = table[nlu].tag;
        PageTable::iterator i = lookupTable.find(oldvpn);

        if (i == lookupTable.end())
            panic("TLB entry not found in lookupTable");

        int index;
        while ((index = i->second) != nlu) {
            if (table[index].tag != oldvpn)
                panic("TLB entry not found in lookupTable");

            ++i;
        }

        DPRINTF(TLB, "remove @%d: %#x -> %#x\n", nlu, oldvpn, table[nlu].ppn);

        lookupTable.erase(i);
    }

    DPRINTF(TLB, "insert @%d: %#x -> %#x\n", nlu, vaddr.vpn(), pte.ppn);

    table[nlu] = pte;
    table[nlu].tag = vaddr.vpn();
    table[nlu].valid = true;

    lookupTable.insert(make_pair(vaddr.vpn(), nlu));
    nextnlu();
  */
}

void
TLB::flushAll()
{
    DPRINTF(TLB, "flushAll\n");
    memset(table, 0, sizeof(MipsISA::PTE[size]));
    lookupTable.clear();
    nlu = 0;
}

void
TLB::serialize(ostream &os)
{
    SERIALIZE_SCALAR(size);
    SERIALIZE_SCALAR(nlu);

    for (int i = 0; i < size; i++) {
        nameOut(os, csprintf("%s.PTE%d", name(), i));
        table[i].serialize(os);
    }
}

void
TLB::unserialize(Checkpoint *cp, const string &section)
{
    UNSERIALIZE_SCALAR(size);
    UNSERIALIZE_SCALAR(nlu);

    for (int i = 0; i < size; i++) {
        table[i].unserialize(cp, csprintf("%s.PTE%d", section, i));
        if (table[i].V0 || table[i].V1) {
            lookupTable.insert(make_pair(table[i].VPN, i));
        }
    }
}

void
TLB::regStats()
{
    read_hits
        .name(name() + ".read_hits")
        .desc("DTB read hits")
        ;

    read_misses
        .name(name() + ".read_misses")
        .desc("DTB read misses")
        ;


    read_accesses
        .name(name() + ".read_accesses")
        .desc("DTB read accesses")
        ;

    write_hits
        .name(name() + ".write_hits")
        .desc("DTB write hits")
        ;

    write_misses
        .name(name() + ".write_misses")
        .desc("DTB write misses")
        ;


    write_accesses
        .name(name() + ".write_accesses")
        .desc("DTB write accesses")
        ;

    hits
        .name(name() + ".hits")
        .desc("DTB hits")
        ;

    misses
        .name(name() + ".misses")
        .desc("DTB misses")
        ;

    invalids
        .name(name() + ".invalids")
        .desc("DTB access violations")
        ;

    accesses
        .name(name() + ".accesses")
        .desc("DTB accesses")
        ;

    hits = read_hits + write_hits;
    misses = read_misses + write_misses;
    accesses = read_accesses + write_accesses;
}

Fault
ITB::translate(RequestPtr &req, ThreadContext *tc)
{
#if !FULL_SYSTEM
    Process * p = tc->getProcessPtr();

    Fault fault = p->pTable->translate(req);
    if(fault != NoFault)
        return fault;

    return NoFault;
#else
  if(MipsISA::IsKSeg0(req->getVaddr()))
    {
      // Address will not be translated through TLB, set response, and go!
      req->setPaddr(MipsISA::KSeg02Phys(req->getVaddr()));
      if(MipsISA::getOperatingMode(tc->readMiscReg(MipsISA::Status)) != mode_kernel || req->isMisaligned())
        {
          AddressErrorFault *Flt = new AddressErrorFault();
          /* BadVAddr must be set */
          Flt->BadVAddr = req->getVaddr();
          return Flt;
        }
    }
  else if(MipsISA::IsKSeg1(req->getVaddr()))
    {
      // Address will not be translated through TLB, set response, and go!
      req->setPaddr(MipsISA::KSeg02Phys(req->getVaddr()));
    }
  else
    {
      /* This is an optimization - smallPages is updated every time a TLB operation is performed
         That way, we don't need to look at Config3 _ SP and PageGrain _ ESP every time we
         do a TLB lookup */
      Addr VPN;
      if(smallPages==1){
        VPN=((req->getVaddr() >> 11));
      } else {
        VPN=((req->getVaddr() >> 11) & 0xFFFFFFFC);
      }
      uint8_t Asid = req->getAsid();
      if(req->isMisaligned()){ // Unaligned address!
        AddressErrorFault *Flt = new AddressErrorFault();
        /* BadVAddr must be set */
        Flt->BadVAddr = req->getVaddr();
        return Flt;
      }
      MipsISA::PTE *pte = lookup(VPN,Asid);
      if(pte != NULL)
        {// Ok, found something
          /* Check for valid bits */
          int EvenOdd;
          bool Valid;
          if((((req->getVaddr()) >> pte->AddrShiftAmount) & 1) ==0){
            // Check even bits
            Valid = pte->V0;
            EvenOdd = 0;
          } else {
            // Check odd bits
            Valid = pte->V1;
            EvenOdd = 1;
          }

          if(Valid == false)
            {//Invalid entry
              ItbInvalidFault *Flt = new ItbInvalidFault();
              /* EntryHi VPN, ASID fields must be set */
              Flt->EntryHi_Asid = Asid;
              Flt->EntryHi_VPN2 = (VPN>>2);
              Flt->EntryHi_VPN2X = (VPN & 0x3);

              /* BadVAddr must be set */
              Flt->BadVAddr = req->getVaddr();

              /* Context must be set */
              Flt->Context_BadVPN2 = (VPN >> 2);
              return Flt;
            }
          else
            {// Ok, this is really a match, set paddr
              //              hits++;
              Addr PAddr;
              if(EvenOdd == 0){
                PAddr = pte->PFN0;
              }else{
                PAddr = pte->PFN1;
              }
              PAddr >>= (pte->AddrShiftAmount-12);
              PAddr <<= pte->AddrShiftAmount;
              PAddr |= ((req->getVaddr()) & pte->OffsetMask);
              req->setPaddr(PAddr);


            }
        }
      else
        { // Didn't find any match, return a TLB Refill Exception
          //      misses++;
          ItbRefillFault *Flt=new ItbRefillFault();
          /* EntryHi VPN, ASID fields must be set */
          Flt->EntryHi_Asid = Asid;
          Flt->EntryHi_VPN2 = (VPN>>2);
          Flt->EntryHi_VPN2X = (VPN & 0x3);


          /* BadVAddr must be set */
          Flt->BadVAddr = req->getVaddr();

          /* Context must be set */
          Flt->Context_BadVPN2 = (VPN >> 2);
          return Flt;
        }
    }
  return checkCacheability(req);
#endif
}

Fault
DTB::translate(RequestPtr &req, ThreadContext *tc, bool write)
{
#if !FULL_SYSTEM
    Process * p = tc->getProcessPtr();

    Fault fault = p->pTable->translate(req);
    if(fault != NoFault)
        return fault;

    return NoFault;
#else
  if(MipsISA::IsKSeg0(req->getVaddr()))
    {
      // Address will not be translated through TLB, set response, and go!
      req->setPaddr(MipsISA::KSeg02Phys(req->getVaddr()));
      if(MipsISA::getOperatingMode(tc->readMiscReg(MipsISA::Status)) != mode_kernel || req->isMisaligned())
        {
          StoreAddressErrorFault *Flt = new StoreAddressErrorFault();
          /* BadVAddr must be set */
          Flt->BadVAddr = req->getVaddr();

          return Flt;
        }
    }
  else if(MipsISA::IsKSeg1(req->getVaddr()))
    {
      // Address will not be translated through TLB, set response, and go!
      req->setPaddr(MipsISA::KSeg02Phys(req->getVaddr()));
    }
  else
    {
      /* This is an optimization - smallPages is updated every time a TLB operation is performed
         That way, we don't need to look at Config3 _ SP and PageGrain _ ESP every time we
         do a TLB lookup */
      Addr VPN=((req->getVaddr() >> 11) & 0xFFFFFFFC);
      if(smallPages==1){
        VPN=((req->getVaddr() >> 11));
      }
      uint8_t Asid = req->getAsid();
      MipsISA::PTE *pte = lookup(VPN,Asid);
      if(req->isMisaligned()){ // Unaligned address!
        StoreAddressErrorFault *Flt = new StoreAddressErrorFault();
        /* BadVAddr must be set */
        Flt->BadVAddr = req->getVaddr();
        return Flt;
      }
      if(pte != NULL)
        {// Ok, found something
          /* Check for valid bits */
          int EvenOdd;
          bool Valid;
          bool Dirty;
          if(((((req->getVaddr()) >> pte->AddrShiftAmount) & 1)) ==0){
            // Check even bits
            Valid = pte->V0;
            Dirty = pte->D0;
            EvenOdd = 0;

          } else {
            // Check odd bits
            Valid = pte->V1;
            Dirty = pte->D1;
            EvenOdd = 1;
          }

          if(Valid == false)
            {//Invalid entry
              //              invalids++;
              DtbInvalidFault *Flt = new DtbInvalidFault();
              /* EntryHi VPN, ASID fields must be set */
              Flt->EntryHi_Asid = Asid;
              Flt->EntryHi_VPN2 = (VPN>>2);
              Flt->EntryHi_VPN2X = (VPN & 0x3);


              /* BadVAddr must be set */
              Flt->BadVAddr = req->getVaddr();

              /* Context must be set */
              Flt->Context_BadVPN2 = (VPN >> 2);

              return Flt;
            }
          else
            {// Ok, this is really a match, set paddr
              //              hits++;
              if(!Dirty)
                {
                  TLBModifiedFault *Flt = new TLBModifiedFault();
                  /* EntryHi VPN, ASID fields must be set */
                  Flt->EntryHi_Asid = Asid;
                  Flt->EntryHi_VPN2 = (VPN>>2);
                  Flt->EntryHi_VPN2X = (VPN & 0x3);


                  /* BadVAddr must be set */
                  Flt->BadVAddr = req->getVaddr();

                  /* Context must be set */
                  Flt->Context_BadVPN2 = (VPN >> 2);
                  return Flt;

                }
              Addr PAddr;
              if(EvenOdd == 0){
                PAddr = pte->PFN0;
              }else{
                PAddr = pte->PFN1;
              }
              PAddr >>= (pte->AddrShiftAmount-12);
              PAddr <<= pte->AddrShiftAmount;
              PAddr |= ((req->getVaddr()) & pte->OffsetMask);
              req->setPaddr(PAddr);
            }
        }
      else
        { // Didn't find any match, return a TLB Refill Exception
          //      misses++;
          DtbRefillFault *Flt=new DtbRefillFault();
          /* EntryHi VPN, ASID fields must be set */
          Flt->EntryHi_Asid = Asid;
          Flt->EntryHi_VPN2 = (VPN>>2);
          Flt->EntryHi_VPN2X = (VPN & 0x3);


          /* BadVAddr must be set */
          Flt->BadVAddr = req->getVaddr();

          /* Context must be set */
          Flt->Context_BadVPN2 = (VPN >> 2);
          return Flt;
        }
    }
    return checkCacheability(req);
#endif
}

///////////////////////////////////////////////////////////////////////
//
//  Mips ITB
//
ITB::ITB(const Params *p)
    : TLB(p)
{}


// void
// ITB::regStats()
// {
//   /*    hits - causes failure for some reason
//      .name(name() + ".hits")
//      .desc("ITB hits");
//     misses
//      .name(name() + ".misses")
//      .desc("ITB misses");
//     acv
//      .name(name() + ".acv")
//      .desc("ITB acv");
//     accesses
//      .name(name() + ".accesses")
//      .desc("ITB accesses");

//      accesses = hits + misses + invalids; */
// }



///////////////////////////////////////////////////////////////////////
//
//  Mips DTB
//
DTB::DTB(const Params *p)
    : TLB(p)
{}

///////////////////////////////////////////////////////////////////////
//
//  Mips UTB
//
UTB::UTB(const Params *p)
    : ITB(p), DTB(p)
{}



MipsISA::PTE &
TLB::index(bool advance)
{
    MipsISA::PTE *pte = &table[nlu];

    if (advance)
        nextnlu();

    return *pte;
}

MipsISA::ITB *
MipsITBParams::create()
{
    return new MipsISA::ITB(this);
}

MipsISA::DTB *
MipsDTBParams::create()
{
    return new MipsISA::DTB(this);
}

MipsISA::UTB *
MipsUTBParams::create()
{
    return new MipsISA::UTB(this);
}
