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

#include <binja/utils/log.h>
#include <binja/utils/settings.h>

#include "macho_task.h"
#include "plugin_macho.h"
#include "source_finder.h"

using namespace Binja;
using namespace DebugInfo;
using namespace BinaryNinja;
using namespace llvm;

namespace fs = std::filesystem;


void PluginMacho::Load(BinaryNinja::DebugInfo &debugInfo, MachOImportProgressMonitor &monitor) {
    auto source = GetSymbolSource();
    if (!source) {
        BDLogDebug("skipping macho symbols import since valid source cannot be found");
        return;
    }

    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {binaryView_.GetObject(), bnSettings->GetObject()};
    BDVerify(settings.MachoEnabled());

    MachOImportOptions options {
        .importFunctions = settings.MachoLoadFunctions(),
        .importDataVariables = settings.MachoLoadDataVariables()
    };

    if (!options.importFunctions) {
        BDLogDebug("skipping macho function symbols import since import functions is disabled");
    }

    if (!options.importDataVariables) {
        BDLogDebug("skipping macho data variable symbols import since import data variables is disabled");
    }

    auto machoObjects = SymbolSourceFinder{*source}.FindAllMachoObjects();
    BDLogInfo("found {} macho symbol sources at {}", machoObjects.size(), source->string());
    MachOImportTask task{std::vector<fs::path>{machoObjects.begin(), machoObjects.end()},
                         binaryView_, debugInfo, options, monitor};
    task.Import();
}

std::optional<fs::path> PluginMacho::GetSymbolSource() {
    auto bnSettings = BinaryNinja::Settings::Instance();
    Utils::BinjaSettings settings {binaryView_.GetObject(), bnSettings->GetObject()};
    BDVerify(settings.MachoEnabled());

    if (auto path = settings.DebugInfoSymbolsSearchPath()) {
        if (!fs::exists(*path)) {
            BDLogError("skipping macho import since specified symbols directory {} does not exist", *path);
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
    return std::nullopt;
}


/// Progress monitor

namespace {

class ImportProgressMonitor : public MachOImportProgressMonitor {
private:
    using Callback = bool (*)(void *, size_t, size_t);

public:
    ImportProgressMonitor(Callback cb, void *pctx) : cb_{cb}, pctx_{pctx} {}

    bool operator()(size_t done, size_t total) override {
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
    if (!settings.MachoEnabled()) {
        BDLogInfo("skipping Mach-O debug info import since it is disabled");
        return false;
    }

    PluginMacho plugin{bv};
    if (plugin.GetSymbolSource()) {
        return true;
    }

    BDLogInfo("skipping import since no valid symbol source found");
    return false;
}

bool DoParseDebugInfo(void *context, BNDebugInfo *debugInfoHandle, BNBinaryView *binaryViewHandle, bool(progress)(void *, size_t, size_t), void *pctx) {
    BinaryNinja::DebugInfo debugInfo{debugInfoHandle};
    BinaryNinja::BinaryView binaryView{binaryViewHandle};

    PluginMacho plugin{binaryView};
    ImportProgressMonitor monitor{progress, pctx};
    plugin.Load(debugInfo, monitor);
    return true;
}

}// namespace

void PluginMacho::RegisterPlugin() {
    BNRegisterDebugInfoParser(kPluginName, IsValidForBinaryView, DoParseDebugInfo, nullptr);
}
