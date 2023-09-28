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


#include <llvm/Demangle/ItaniumDemangle.h>
#include <type_traits>

#include <binja/macho/macho.h>
#include <binja/utils/demangle.h>
#include <binja/utils/log.h>
#include <binja/utils/settings.h>

#include "plugin_symtab.h"

using namespace Binja;
using namespace DebugInfo;

namespace BN = BinaryNinja;
namespace LD = llvm::itanium_demangle;

// LLVM demangler

namespace {
class BumpPointerAllocator {
    struct BlockMeta {
        BlockMeta* Next;
        size_t Current;
    };

    static constexpr size_t AllocSize = 4096;
    static constexpr size_t UsableAllocSize = AllocSize - sizeof(BlockMeta);

    alignas(long double) char InitialBuffer[AllocSize];
    BlockMeta* BlockList = nullptr;

    void grow() {
        char* NewMeta = static_cast<char *>(std::malloc(AllocSize));
        if (NewMeta == nullptr)
            std::terminate();
        BlockList = new (NewMeta) BlockMeta{BlockList, 0};
    }

    void* allocateMassive(size_t NBytes) {
        NBytes += sizeof(BlockMeta);
        BlockMeta* NewMeta = reinterpret_cast<BlockMeta*>(std::malloc(NBytes));
        if (NewMeta == nullptr)
            std::terminate();
        BlockList->Next = new (NewMeta) BlockMeta{BlockList->Next, 0};
        return static_cast<void*>(NewMeta + 1);
    }

public:
    BumpPointerAllocator()
        : BlockList(new (InitialBuffer) BlockMeta{nullptr, 0}) {}

    void* allocate(size_t N) {
        N = (N + 15u) & ~15u;
        if (N + BlockList->Current >= UsableAllocSize) {
            if (N > UsableAllocSize)
                return allocateMassive(N);
            grow();
        }
        BlockList->Current += N;
        return static_cast<void*>(reinterpret_cast<char*>(BlockList + 1) +
                                   BlockList->Current - N);
    }

    void reset() {
        while (BlockList) {
            BlockMeta* Tmp = BlockList;
            BlockList = BlockList->Next;
            if (reinterpret_cast<char*>(Tmp) != InitialBuffer)
                std::free(Tmp);
        }
        BlockList = new (InitialBuffer) BlockMeta{nullptr, 0};
    }

    ~BumpPointerAllocator() { reset(); }
};

class DefaultAllocator {
    BumpPointerAllocator Alloc;

public:
    void reset() { Alloc.reset(); }

    template<typename T, typename ...Args> T *makeNode(Args &&...args) {
        return new (Alloc.allocate(sizeof(T)))
            T(std::forward<Args>(args)...);
    }

    void *allocateNodeArray(size_t sz) {
        return Alloc.allocate(sizeof(LD::Node *) * sz);
    }
};
}  // unnamed namespace

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

std::string NodeToString(const LD::Node* node) {
    LD::OutputBuffer buffer;
    LD::initializeOutputBuffer(nullptr, nullptr, buffer, 512);
    node->print(buffer);
    std::string result {buffer.getBuffer(), buffer.getCurrentPosition()};
    free(buffer.getBuffer());
    return result;
}

std::optional<BN::DebugFunctionInfo> ParseMangledFunctionInfo(const MachO::Symbol& symbol) {
    std::string name = symbol.name;

    bool isMangled = name.starts_with("_Z");
    if (!isMangled) {
        return std::nullopt;
    }

    LD::ManglingParser<DefaultAllocator> parser{
        name.c_str(),
        name.c_str() + name.size()
    };

    LD::Node* root = parser.parse();
    if (!root) {
        return std::nullopt;
    }

    if (root->getKind() != LD::Node::KFunctionEncoding) {
        return std::nullopt;
    }

    auto* func = static_cast<LD::FunctionEncoding*>(root);
    std::string functionName = NodeToString(func->getName());
    return BN::DebugFunctionInfo{
        functionName,
        Utils::Demangle(name),
        name,
        symbol.addr,
        nullptr,
        nullptr,
    };
}

BN::DebugFunctionInfo ParseFunctionInfo(const MachO::Symbol& symbol) {
    if (auto info = ParseMangledFunctionInfo(symbol)) {
        return *info;
    }

    std::string name = symbol.name;

    return BN::DebugFunctionInfo {
        name,
        name,
        name,
        symbol.addr,
        nullptr,
        nullptr,
    };
}

template <typename ...Extra>
struct DoParseDebugInfoImpl {
    static bool Invoke(void *context, BNDebugInfo *debugInfoHandle, BNBinaryView *binaryViewHandle, Extra..., bool(progress)(void *, size_t, size_t), void *pctx) {
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
        MachO::MachBinaryViewDataBackend dataBackend{*rawView};

        std::vector<MachO::Fileset> filesets = MachO::MachHeaderParser{dataBackend, 0}.DecodeFilesets();
        for (size_t i=0; i<filesets.size(); ++i) {
            MachO::Fileset &fileset = filesets[i];
            MachO::MachHeaderParser parser{dataBackend, fileset.fileOffset};
            std::vector<MachO::Symbol> symbols = parser.DecodeSymbols();

            for (auto &symbol: symbols) {
                BN::Ref<BN::Segment> segment = binaryView.GetSegmentAt(symbol.addr);
                if (!segment) {
                    BDLogDebug("ignoring nlist_64 entry, n_value {:#016x} is not in any segment", symbol.addr);
                    continue;
                }
                bool isFunction = segment->GetFlags() & BNSegmentFlag::SegmentContainsCode;
                if (isFunction && settings.SymtabLoadFunctions()) {
                    debugInfo.AddFunction(ParseFunctionInfo(symbol));
                } else if (settings.SymtabLoadDataVariables()) {
                    debugInfo.AddDataVariable(symbol.addr, BN::Type::VoidType(), symbol.name);
                }
            }
            progress(pctx, i, filesets.size());
        }
        return true;
    }
};
using DoParseDebugInfo = std::conditional_t<BN_CURRENT_CORE_ABI_VERSION >= 35, DoParseDebugInfoImpl<BNBinaryView *>, DoParseDebugInfoImpl<>>;

}// namespace

void PluginSymtab::RegisterPlugin() {
    BNRegisterDebugInfoParser(kPluginName, IsValidForBinaryView, DoParseDebugInfo::Invoke, nullptr);
}
