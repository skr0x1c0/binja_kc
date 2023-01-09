// Copyright (c) skr0x1c0 2022.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#pragma once

#include <string>

#include <binaryninjaapi.h>
#include <binaryninjacore.h>

#include "../types/errors.h"
#include "../types/uuid.h"
#include "../utils/binary_view.h"

namespace Binja::MachO {

class MachHeaderDecodeError : public Types::DecodeError {
    using Types::DecodeError::DecodeError;
};

struct Fileset {
    std::string name;
    uint64_t vmAddr;
    uint64_t fileOffset;
};

struct Section {
    std::string name;
    uint64_t vaStart;
    uint64_t vaLength;
    BNSectionSemantics semantics;
};

struct Segment {
    std::string name;
    uint64_t vaStart;
    uint64_t vaLength;
    uint64_t dataStart;
    uint64_t dataLength;
    uint32_t flags;
    std::vector<Section> sections;
};

struct Symbol {
    std::string name;
    uint64_t addr;
};

class MachHeaderParser {
public:
    MachHeaderParser(BinaryNinja::BinaryView &binaryView, uint64_t machHeaderOffset)
        : binaryView_{binaryView}, machHeaderOffset_{machHeaderOffset} {
        VerifyHeader();
    }

    std::vector<Fileset> DecodeFilesets();
    std::vector<Segment> DecodeSegments();
    std::optional<uint64_t> DecodeEntryPoint();
    std::optional<Types::UUID> DecodeUUID();
    std::vector<Symbol> DecodeSymbols();

private:
    void VerifyHeader();
    template <class T> std::optional<T> FindCommand(uint32_t cmd);

    static Fileset DecodeFileset(Utils::BinaryViewDataReader &reader);
    static Segment DecodeSegment(Utils::BinaryViewDataReader &reader);
    static std::vector<Section> DecodeSections(Utils::BinaryViewDataReader &reader);

private:
    BinaryNinja::BinaryView &binaryView_;
    uint64_t machHeaderOffset_;
};


class MachBinaryView {
public:
    MachBinaryView(BinaryNinja::BinaryView &binaryView) : binaryView_{binaryView} {}
    std::map<Types::UUID, std::vector<Segment>> ReadMachOHeaders();
    std::vector<uint64_t> ReadMachOHeaderOffsets();

private:
    BinaryNinja::BinaryView &binaryView_;
};

}// namespace Binja::MachO
