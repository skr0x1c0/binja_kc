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


#include <binaryninjaapi.h>

#include <binja/utils/log.h>

#include "function.h"

using namespace Binja;
using namespace DebugInfo;

using namespace BinaryNinja;
namespace DW = llvm::dwarf;

std::optional<DwarfFunctionInfo> FunctionDecoder::Decode() {
    DwarfFunctionInfo info;
    if (auto entry = DecodeEntryPoint()) {
        info.entryPoint = *entry;
    } else {
        return std::nullopt;
    }

    if (auto slidAddress = ctx_.SlideAddress(die_.GetOffset(), info.entryPoint)) {
        info.entryPoint = *slidAddress;
    } else {
        BDLogWarn("cannot slide address {:#016x} using binary {}",
                  info.entryPoint, die_.GetOffset().binaryId);
        return std::nullopt;
    }

    info.qualifiedName = ctx_.DecodeQualifiedName(die_);
    info.type = FunctionTypeBuilder{ctx_, die_}.Build();
    info.isNoReturn = DecodeIsNoReturn();

    return info;
}

std::optional<uint64_t> FunctionDecoder::DecodeEntryPoint() {
    const AttributeReader &attributeReader = dieReader_.AttrReader();
    if (auto value = attributeReader.ReadUInt(DW::DW_AT_low_pc)) {
        return value;
    }
    if (auto ranges = die_.GetAddressRanges()) {
        if (ranges->size() != 0) {
            return ranges.get().begin()->LowPC;
        }
    }
    if (auto value = attributeReader.ReadUInt(DW::DW_AT_entry_pc)) {
        return value;
    }
    return std::nullopt;
}

bool FunctionDecoder::DecodeIsNoReturn() {
    return dieReader_.AttrReader().HasAttribute(DW::DW_AT_noreturn, true);
}
