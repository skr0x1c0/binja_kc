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


#include <filesystem>

#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <fmt/format.h>

#include <binja/macho/macho.h>
#include <binja/utils/log.h>
#include <binja/utils/settings.h>

#include "debug.h"
#include "dsym.h"
#include "dwarf_task.h"
#include "plugin_dsym.h"
#include "source_finder.h"

using namespace Binja;
using namespace DebugInfo;
using namespace BinaryNinja;
using namespace llvm;

namespace fs = std::filesystem;


void PluginDSYM::Load(BinaryNinja::DebugInfo &debugInfo, DwarfImportProgressMonitor &monitor) {
    auto source = GetSymbolSource();
    if (!source) {
        BDLogDebug("skipping dwarf symbols importing since no "
                   "dwarf source can be found");
        return;
    }

    SymbolSourceFinder sourceFinder{*source};

    std::vector<fs::path> dwarfObjects;
    for (const auto &dSYMFile: sourceFinder.FindAllDSYMObjects()) {
        try {
            auto objects = DwarfObjectFile::DsymFindObjects(dSYMFile);
            dwarfObjects.insert(dwarfObjects.end(), objects.begin(), objects.end());
        } catch (const DwarfError &e) {
            BDLogError("failed to open symbols file {}, error: {}",
                       dSYMFile.string(), e.what());
            return;
        }
    }

    auto targetObjects = MachO::MachBinaryView{binaryView_}.ReadMachOHeaders();
    std::vector<fs::path> sourceObjects;

    for (const auto &dwarfObject: dwarfObjects) {
        DwarfObjectFile objectFile{dwarfObject};
        auto uuid = objectFile.DecodeUUID();
        if (!uuid) {
            BDLogWarn("ignoring dwarf object {} since it does not have LC_UUID",
                      dwarfObject.string());
            continue;
        }

        if (!targetObjects.contains(*uuid)) {
            BDLogWarn("ignoring dwarf object {} since its uuid does not match with "
                      "any macho headers in binary view",
                      dwarfObject.string());
            continue;
        }

        sourceObjects.push_back(dwarfObject);
    }

    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {binaryView_.GetObject(), bnSettings->GetObject()};

    BDVerify(settings.DWARFEnabled());
    ImportOptions options{
        .importTypes = settings.DWARFLoadTypes(),
        .importFunctions = settings.DWARFLoadFunctions(),
        .importGlobals = settings.DWARFLoadDataVariables(),
    };

    BDLogInfo("found {} dwarf symbols sources at {}", dwarfObjects.size(), source->string());
    try {
        DwarfImportTask task{sourceObjects, binaryView_, debugInfo, options, monitor};
        task.Import();
    } catch (const Types::DecodeError &e) {
        BDLogError("Failed to load symbols, error: {}", e.what());
    }
}

std::optional<fs::path> PluginDSYM::GetSymbolSource() {
    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {binaryView_.GetObject(), bnSettings->GetObject()};
    BDVerify(settings.DWARFEnabled());

    if (auto path = settings.DebugInfoSymbolsSearchPath()) {
        if (!fs::exists(*path)) {
            BDLogError("skipping dwarf import since specified symbols directory {} does not exist", *path);
            return std::nullopt;
        }
        return *path;
    }

    fs::path binarySource = binaryView_.GetFile()->GetOriginalFilename();

    fs::path symbolsDirectory = fmt::format("{}.symbols", binarySource.string());
    if (fs::exists(symbolsDirectory)) {
        return symbolsDirectory;
    }
    BDLogInfo("no symbols source directory found at {}", symbolsDirectory.string());

    fs::path dsym = fmt::format("{}.dSYM", binarySource.string());

    if (fs::exists(dsym)) {
        return dsym;
    }
    BDLogInfo("no dSYM found at {}", dsym.string());

    return std::nullopt;
}

/// Progress monitor

namespace {

class ImportProgressMonitor : public DwarfImportProgressMonitor {
private:
    using Callback = bool (*)(void *, size_t, size_t);

public:
    explicit ImportProgressMonitor(Callback cb, void *pctx) : cb_{cb}, pctx_{pctx} {}

    bool operator()(DwarfImportPhase phase, size_t done, size_t total) override {
        return cb_(pctx_, done, total);
    }

private:
    Callback cb_;
    void *pctx_;
};

}// namespace


/// Binary ninja plugin API

namespace {

bool IsValidForBinaryView(void *context, BNBinaryView *handle) {
    BinaryNinja::BinaryView bv{handle};

    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {bv.GetObject(), bnSettings->GetObject()};
    if (!settings.DWARFEnabled()) {
        BDLogInfo("skipping dsym debug info import since it is disabled");
        return false;
    }

    PluginDSYM plugin{bv};
    if (plugin.GetSymbolSource()) {
        return true;
    }

    BDLogInfo("skipping dsym debug info import since no valid symbol source found");
    return false;
}

bool DoParseDebugInfo(void *context, BNDebugInfo *debugInfoHandle, BNBinaryView *binaryViewHandle, bool(progress)(void *, size_t, size_t), void *pctx) {
    BinaryNinja::DebugInfo debugInfo{debugInfoHandle};
    BinaryNinja::BinaryView binaryView{binaryViewHandle};

    ImportProgressMonitor monitor{progress, pctx};
    PluginDSYM plugin{binaryView};
    plugin.Load(debugInfo, monitor);
    return true;
}

}// namespace

void PluginDSYM::RegisterPlugin() {
    BNRegisterDebugInfoParser(kPluginName, IsValidForBinaryView, DoParseDebugInfo, nullptr);
}
