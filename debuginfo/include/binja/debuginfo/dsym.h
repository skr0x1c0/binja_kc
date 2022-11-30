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

#include <filesystem>
#include <string>
#include <vector>

#include <binja/macho/macho.h>
#include <binja/types/uuid.h>

#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/Object/MachO.h>

namespace Binja::DebugInfo {

class DwarfObjectFile {
public:
    explicit DwarfObjectFile(const std::filesystem::path &objectPath);
    llvm::DWARFContext &GetDWARFContext() { return *dwarfContext_; }

    static std::vector<std::filesystem::path> DsymFindObjects(
        const std::filesystem::path &symbolsPath);

    std::optional<Types::UUID> DecodeUUID() const;
    std::vector<MachO::Segment> DecodeSegments() const;

private:
    std::unique_ptr<llvm::MemoryBuffer> buffer_;
    std::unique_ptr<llvm::object::Binary> binaryObject_;
    std::unique_ptr<llvm::DWARFContext> dwarfContext_;
};

}// namespace Binja::DebugInfo
