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

#include <llvm/DebugInfo/DWARF/DWARFContext.h>

#include <binja/utils/log.h>

#include "variable.h"

using namespace Binja;
using namespace DebugInfo;

namespace DW = llvm::dwarf;
namespace BN = BinaryNinja;

std::optional<DwarfVariableInfo> VariableDecoder::Decode() {
    auto &attributeReader = dieReader_.AttrReader();
    DwarfVariableInfo info;

    if (auto location = attributeReader.ReadLocationAddress()) {
        info.location = *location;
    } else {
        return std::nullopt;
    }

    if (auto slidLocation = ctx_.SlideAddress(die_.GetOffset(), info.location)) {
        info.location = *slidLocation;
    } else {
        BDLogDebug("cannot slide data symbol address {}", info.location);
        return std::nullopt;
    }

    std::string name = attributeReader.ReadName("", true);
    if (name.empty()) {
        BDLogDebug("ignoring variable with no name, DIE: {}", dieReader_.Dump());
        return std::nullopt;
    }
    info.qualifiedName = ctx_.DecodeQualifiedName(die_);

    auto valueType = attributeReader.ReadReference(DW::DW_AT_type);
    if (valueType) {
        info.type = GenericTypeBuilder{ctx_, *valueType}.Build();
    } else {
        BDLogWarn("encountered variable with no type, DIE: {}", dieReader_.Dump());
        info.type = BN::Type::VoidType();
    }
    return info;
}