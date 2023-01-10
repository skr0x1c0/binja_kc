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


#include <mutex>

#include <binja/macho/macho.h>
#include <binja/utils/log.h>
#include <binja/utils/settings.h>

#include "plugin_symtab.h"

using namespace Binja;
using namespace DebugInfo;

namespace BN = BinaryNinja;

/// Binary ninja plugin API

namespace {

bool IsValidForBinaryView(void *context, BNBinaryView *handle) {
    BinaryNinja::BinaryView bv{handle};

    if (bv.GetTypeName() != "MachO-KC") {
        return false;
    }

    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {bv.GetObject(), bnSettings->GetObject()};

    if (!settings.SymtabEnabled()) {
        BDLogInfo("skipping KC SYMTAB debug info import since it is disabled");
        return false;
    }

    return true;
}

bool DoParseDebugInfo(void *context, BNDebugInfo *debugInfoHandle, BNBinaryView *binaryViewHandle, bool(progress)(void *, size_t, size_t), void *pctx) {
    BN::BinaryView binaryView{binaryViewHandle};
    BN::Ref<BN::BinaryView> rawView = binaryView.GetParentView();

    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {binaryView.GetObject(), bnSettings->GetObject()};

    if (!settings.SymtabLoadFunctions()) {
        BDLogInfo("functions debug info import from KC SYMTAB is disabled");
    }

    if (!settings.SymtabLoadDataVariables()) {
        BDLogInfo("data variables debug info import from KC SYMTAB is disabled");
    }

    BN::DebugInfo debugInfo{debugInfoHandle};
    std::vector<MachO::Fileset> filesets = MachO::MachHeaderParser{*rawView, 0}.DecodeFilesets();

    for (size_t i=0; i<filesets.size(); ++i) {
        MachO::Fileset &fileset = filesets[i];
        MachO::MachHeaderParser parser{*rawView, fileset.fileOffset};
        std::vector<MachO::Symbol> symbols = parser.DecodeSymbols();

        for (auto &symbol: symbols) {
            BN::Ref<BN::Segment> segment = binaryView.GetSegmentAt(symbol.addr);
            if (!segment) {
                BDLogDebug("ignoring nlist_64 entry n_value {:#016x} is not in any segment", symbol.addr);
                continue;
            }
            bool isFunction = segment->GetFlags() & BNSegmentFlag::SegmentContainsCode;
            if (symbol.name.starts_with("_")) {
                symbol.name = symbol.name.substr(1);
            }
            if (isFunction && settings.SymtabLoadFunctions()) {
                BN::DebugFunctionInfo info{
                    symbol.name,
                    symbol.name,
                    symbol.name,
                    symbol.addr,
                    nullptr,
                    nullptr,
                };
                debugInfo.AddFunction(info);
            } else if (settings.SymtabLoadDataVariables()) {
                debugInfo.AddDataVariable(symbol.addr, BN::Type::VoidType(), symbol.name);
            }
        }
        progress(pctx, i, filesets.size());
    }
    return true;
}

}// namespace

void PluginSymtab::RegisterPlugin() {
    BNRegisterDebugInfoParser(kPluginName, IsValidForBinaryView, DoParseDebugInfo, nullptr);
}
