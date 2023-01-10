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
#include <mutex>

#include <binaryninjaapi.h>
#include <binaryninjacore.h>

#include <taskflow/taskflow.hpp>

#include <binja/utils/binary_view.h>
#include <binja/utils/debug.h>
#include <binja/utils/log.h>

#include "macho_task.h"

using namespace Binja;
using namespace DebugInfo;
using namespace BinaryNinja;
namespace fs = std::filesystem;


/// MachO import task

MachOImportTask::MachOImportTask(std::vector<fs::path> sources, BinaryView &binaryView,
                                 BinaryNinja::DebugInfo &debugInfo,
                                 MachOImportOptions options, MachOImportProgressMonitor &monitor)
    : binaryView_{binaryView}, debugInfo_{debugInfo}, sources_{sources},
      options_{options}, monitor_{monitor},
      targetSegments_{MachO::MachBinaryView{binaryView}.ReadMachOHeaders()} {
    for (const auto &symbol: binaryView.GetSymbols()) {
        registeredSymbols_[symbol->GetAddress()] = symbol->GetFullName();
    }
}

void MachOImportTask::Import() {
    tf::Taskflow taskflow;
    tf::Executor executor;

    std::mutex mtx;
    size_t numAdded = 0;
    std::atomic<size_t> completed;
    taskflow.for_each(sources_.begin(), sources_.end(), [&](const auto &source) {
        auto binary = OpenMachO(source);
        completed++;
        if (!binary) {
            return;
        }

        BDLogDebug("importing symbols from macho {}", binary->GetFile()->GetOriginalFilename());
        auto uuid = MachO::MachHeaderParser{*binary, binary->GetStart()}.DecodeUUID();
        BDVerify(uuid);
        auto targetSegments = targetSegments_[*uuid];

        AddressSlider slider = AddressSlider::CreateFromMachOSegments(
            MachO::MachHeaderParser{*binary, binary->GetStart()}.DecodeSegments(),
            targetSegments);

        std::vector<Ref<Symbol>> symbols = binary->GetSymbols();
        std::lock_guard lock{mtx};
        monitor_(completed, sources_.size());

        for (Ref<Symbol> symbol: symbols) {
            if (AddSymbol(*symbol, slider)) {
                numAdded++;
            }
        }
    });

    executor.run(taskflow).wait();
    BDLogInfo("Imported {} symbols from {} macho sources", numAdded, sources_.size());
}

Ref<BinaryView> MachOImportTask::OpenMachO(const fs::path &path) {
    Json::Value options;
    Json::Value preferredArchs;
    preferredArchs.append("arm64e");
    preferredArchs.append("arm64");
    options["files.universal.architecturePreference"] = preferredArchs;// TODO: dynamic
    options["analysis.debugInfo.internal"] = false;
    auto bv = Utils::OpenBinaryView(path, false, nullptr, nullptr, options);
    if (!bv->HasSymbols()) {
        BDLogWarn("ignoring macho image {} with no symbols", path.string());
        return nullptr;
    }
    auto uuid = MachO::MachHeaderParser{*bv, bv->GetStart()}.DecodeUUID();
    if (!uuid) {
        BDLogWarn("ignoring macho image {} with no LC_UUID", path.string());
        return nullptr;
    }
    if (!targetSegments_.contains(*uuid)) {
        BDLogDebug("ignoring macho image {} with uuid {} since its uuid does not match with any "
                   "segment in binary view",
                   path.string(), *uuid);
        return nullptr;
    }
    return bv;
}

bool MachOImportTask::AddSymbol(const Symbol &symbol, AddressSlider &slider) {
    std::string qualifiedName = symbol.GetFullName();
    uint64_t address = symbol.GetAddress();

    if (symbol.GetType() != BNSymbolType::FunctionSymbol && symbol.GetType() != BNSymbolType::DataSymbol) {
        BDLogDebug("ignoring external symbol {} at {}",
                   symbol.GetFullName(), symbol.GetAddress());
        return false;
    }

    if (symbol.GetType() == BNSymbolType::FunctionSymbol && !options_.importFunctions) {
        return false;
    }

    if (symbol.GetType() == BNSymbolType::DataSymbol && !options_.importDataVariables) {
        return false;
    }

    if (auto slidAddress = slider.SlideAddress(address)) {
        address = *slidAddress;
    } else {
        BDLogWarn("failed to slide address {}", address);
        return false;
    }

    if (registeredSymbols_.contains(address)) {
        BDLogWarn("skipping symbol {} since another symbol {} already exist at address {:#016x}",
                  qualifiedName, registeredSymbols_[address], address);
        return false;
    }

    Ref<Symbol> newSymbol = new Symbol{
        symbol.GetType(),
        qualifiedName,
        address,
        symbol.GetBinding(),
        symbol.GetNameSpace()};

    registeredSymbols_[address] = qualifiedName;

    switch (symbol.GetType()) {
        case FunctionSymbol: {
            DebugFunctionInfo info{
                symbol.GetShortName(),
                symbol.GetFullName(),
                symbol.GetRawName(),
                address,
                nullptr,
                binaryView_.GetDefaultPlatform(),
            };
            debugInfo_.AddFunction(info);
            break;
        }
        case DataSymbol: {
            debugInfo_.AddDataVariable(address, Type::VoidType(), symbol.GetFullName());
            break;
        }
        case ImportAddressSymbol:
        case ImportedFunctionSymbol:
        case ImportedDataSymbol:
        case ExternalSymbol:
        case LibraryFunctionSymbol:
            BDVerify(false);
    }
    return true;
}
