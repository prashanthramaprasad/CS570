/*
 * Copyright (c) 2003 The Regents of The University of Michigan
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

#ifndef __OBJECT_FILE_HH__
#define __OBJECT_FILE_HH__

#include "isa_traits.hh"	// for Addr

class FunctionalMemory;
class SymbolTable;

class ObjectFile
{
  protected:
    const std::string filename;
    int descriptor;
    uint8_t *fileData;
    size_t len;

    ObjectFile(const std::string &_filename, int _fd,
               size_t _len, uint8_t *_data);

  public:
    virtual ~ObjectFile();

    void close();

    virtual bool loadSections(FunctionalMemory *mem,
                              bool loadPhys = false) = 0;
    virtual bool loadGlobalSymbols(SymbolTable *symtab) = 0;
    virtual bool loadLocalSymbols(SymbolTable *symtab) = 0;

  protected:

    struct Section {
        Addr baseAddr;
        size_t size;
    };

    Addr entry;
    Addr globalPtr;

    Section text;
    Section data;
    Section bss;

  public:
    Addr entryPoint() const { return entry; }
    Addr globalPointer() const { return globalPtr; }

    Addr textBase() const { return text.baseAddr; }
    Addr dataBase() const { return data.baseAddr; }
    Addr bssBase() const { return bss.baseAddr; }

    size_t textSize() const { return text.size; }
    size_t dataSize() const { return data.size; }
    size_t bssSize() const { return bss.size; }
};

ObjectFile *createObjectFile(const std::string &fname);


#endif // __OBJECT_FILE_HH__
