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

#include <llvm/DebugInfo/DWARF/DWARFContext.h>

#include "dwarf.h"

namespace Binja::DebugInfo {

struct ImportOptions {
    bool importTypes;
    bool importFunctions;
    bool importGlobals;
};

enum class DwarfImportPhase : int {
    Min = -1,
    IndexingQualifiedNames,
    DecodingTypes,
    AddingTypesToBinaryView,
    ImportingFunctionsAndGlobals,
    Max
};

struct DwarfImportProgressMonitor {
    virtual bool operator()(DwarfImportPhase phase, size_t total, size_t done) = 0;
};

class DwarfImportTask {
public:
    DwarfImportTask(const std::vector<std::filesystem::path> &dwarfObjects,
                    BinaryNinja::BinaryView &binaryView,
                    BinaryNinja::DebugInfo &debugInfo,
                    ImportOptions options,
                    DwarfImportProgressMonitor &monitor)
        : dwarfObjects_{dwarfObjects},
          binaryView_{binaryView},
          debugInfo_{debugInfo},
          options_{options},
          monitor_{monitor} {}

    const ImportOptions &GetImportOptions() { return options_; }
    void Import();
    static bool IsNamedTypeTag(llvm::dwarf::Tag tag);

private:
    DwarfContextWrapper BuildDwarfContext();

private:
    const std::vector<std::filesystem::path> &dwarfObjects_;
    BinaryNinja::BinaryView &binaryView_;
    BinaryNinja::DebugInfo &debugInfo_;
    ImportOptions options_;
    DwarfImportProgressMonitor &monitor_;
};

}// namespace Binja::DebugInfo