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

#include <binaryninjaapi.h>

#include <llvm/DebugInfo/DWARF/DWARFDie.h>

#include "dwarf.h"
#include "types.h"

namespace Binja::DebugInfo {

struct DwarfFunctionInfo {
    BinaryNinja::Ref<BinaryNinja::Type> type;
    BinaryNinja::QualifiedName qualifiedName;
    uint64_t entryPoint;
    bool isNoReturn;
};

class FunctionDecoder {
public:
    FunctionDecoder(TypeBuilderContext &ctx, DwarfDieWrapper &die)
        : ctx_{ctx}, die_{die}, dieReader_{die_} {}

    std::optional<DwarfFunctionInfo> Decode();

private:
    std::optional<uint64_t> DecodeEntryPoint();
    bool DecodeIsNoReturn();

private:
    TypeBuilderContext &ctx_;
    DwarfDieWrapper &die_;
    DieReader dieReader_;
};

}// namespace Binja::DebugInfo
