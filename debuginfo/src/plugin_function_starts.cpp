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
#include <type_traits>

#include <binja/macho/macho.h>
#include <binja/utils/log.h>
#include <binja/utils/settings.h>

#include "plugin_function_starts.h"

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

    if (!settings.FunctionStartsEnabled()) {
        BDLogInfo("skipping LC_FUNCTION_STARTS debug info import since it is disabled");
        return false;
    }

    return true;
}

template <typename ...Extra>
struct DoParseDebugInfoImpl {
    static bool Invoke(void *context, BNDebugInfo *debugInfoHandle, BNBinaryView *binaryViewHandle, Extra..., bool(progress)(void *, size_t, size_t), void *pctx) {
        BN::BinaryView binaryView{binaryViewHandle};
        BN::Ref<BN::BinaryView> rawView = binaryView.GetParentView();

        auto bnSettings = BinaryNinja::Settings::Instance();
        Utils::BinjaSettings settings {binaryView.GetObject(), bnSettings->GetObject()};
        BDVerify(settings.FunctionStartsEnabled());

        BN::DebugInfo debugInfo{debugInfoHandle};
        MachO::MachBinaryViewDataBackend dataBackend{*rawView};

        std::vector<MachO::Fileset> filesets = MachO::MachHeaderParser{dataBackend, 0}.DecodeFilesets();
        for (size_t i=0; i<filesets.size(); ++i) {
            MachO::Fileset &fileset = filesets[i];
            uint64_t addr;
            if (!binaryView.GetAddressForDataOffset(fileset.fileOffset, addr)) {
                continue;
            }

            MachO::MachHeaderParser parser{dataBackend, fileset.fileOffset};
            std::vector<uint64_t> functionStarts = parser.DecodeFunctionStarts();
            BDLogInfo("found {} entries from LC_FUNCTION_START in fileset {}", functionStarts.size(), fileset.name);

            for (auto start: functionStarts) {
                BN::Ref<BN::Segment> segment = binaryView.GetSegmentAt(start);
                if (!segment) {
                    BDLogDebug("ignoring LC_FUNCTION_START entry {:#016x} is not in any segment", start);
                    continue;
                }
                bool isFunction = segment->GetFlags() & BNSegmentFlag::SegmentContainsCode;
                if (!isFunction) {
                    BDLogWarn("ignoring LC_FUNCTION_START entry {:#016x} since it is not in segment "
                              "with SegmentContainsCode", start);
                    continue;
                }

                std::string name = fmt::format("sub_{:x}", start);
                BN::DebugFunctionInfo info{
                    name,
                    name,
                    name,
                    start,
                    nullptr,
                    nullptr,
                    {},
                    {}
                };
                debugInfo.AddFunction(info);
            }
            progress(pctx, i, filesets.size());
        }
        return true;
    }
};
using DoParseDebugInfo = std::conditional_t<BN_CURRENT_CORE_ABI_VERSION >= 35, DoParseDebugInfoImpl<BNBinaryView *>, DoParseDebugInfoImpl<>>;

}// namespace

void PluginFunctionStarts::RegisterPlugin() {
    BNRegisterDebugInfoParser(kPluginName, IsValidForBinaryView, DoParseDebugInfo::Invoke, nullptr);
}
