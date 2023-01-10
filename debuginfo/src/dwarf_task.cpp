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

#include <llvm/DebugInfo/DWARF/DWARFDie.h>

#include <binja/utils/debug.h>
#include <binja/utils/log.h>

#include "debug.h"
#include "dwarf_task.h"
#include "function.h"
#include "name_index.h"
#include "types.h"
#include "variable.h"

using namespace Binja;
using namespace DebugInfo;
using namespace llvm;
using namespace BinaryNinja;

namespace {

class OrderedTypeBuilderContext : public TypeBuilderContext {
public:
    OrderedTypeBuilderContext(DwarfContextWrapper &dwarfContext, NameIndex &index)
        : TypeBuilderContext{dwarfContext}, index_{index} {}

    QualifiedName DecodeQualifiedName(DwarfDieWrapper &die) {
        return index_.DecodeQualifiedName(die);
    }

    DwarfDieWrapper ResolveDie(DwarfDieWrapper &die) {
        return index_.ResolveDieOffset(die.GetOffset());
    }

private:
    NameIndex index_;
};

}// namespace

void DwarfImportTask::Import() {
    DwarfContextWrapper dwarfContext = BuildDwarfContext();
    BDLogInfo("importing symbols from {} dwarf objects",
              dwarfContext.GetDwarfObjectCount());

    NameIndex nameIndex{dwarfContext};

    // phase 1
    {
        const auto &units = dwarfContext.GetNormalUnitsVector();
        size_t numUnits = units.size();
        BDLogInfo("indexing types from {} units", numUnits);

        for (size_t i = 0; i < numUnits; ++i) {
            for (const auto &dieInfo: units[i].Dies()) {
                DwarfDieWrapper die = dwarfContext.GetDIEForOffset(dieInfo.GetOffset());
                if (!IsNamedTypeTag(die.GetTag())) {
                    continue;
                }
                if (AttributeReader{die}.ReadName("", true).empty()) {
                    continue;
                }
                nameIndex.IndexDie(die);
            }
            monitor_(DwarfImportPhase::IndexingQualifiedNames, i, numUnits);
        }
    }

    if (options_.importTypes) {
        // phase 2
        size_t numNamedNodes = nameIndex.NumEntries();
        OrderedTypeBuilderContext context{dwarfContext, nameIndex};
        BDLogInfo("indexed {} named entities", numNamedNodes);
        size_t index = 0;
        nameIndex.VisitEntries([&](const std::vector<std::string> &qualifiedName, DwarfOffset dieOffset) {
            DwarfDieWrapper die = dwarfContext.GetDIEForOffset(dieOffset);
            if (IsNamedTypeTag(die.GetTag()) && !AttributeReader{die}.ReadName("", true).empty()) {
                auto type = GenericTypeBuilder{context, die, true}.Build();
                auto name = QualifiedName{qualifiedName};
                debugInfo_.AddType(name.GetString(), type);
            }
            monitor_(DwarfImportPhase::DecodingTypes, ++index, ++numNamedNodes);
        });
        BDLogInfo("imported {} named types to binary view", numNamedNodes);
    } else {
        BDLogInfo("skipping type import");
    }

    // phase 3
    {
        const auto &units = dwarfContext.GetNormalUnitsVector();
        size_t numUnits = units.size();
        BDLogInfo("importing functions and globals from {} units", numUnits);

        OrderedTypeBuilderContext context{dwarfContext, nameIndex};
        std::set<uint64_t> importedFunctions;
        std::set<uint64_t> importedGlobals;
        for (size_t i = 0; i < numUnits; ++i) {
            for (const auto &dieInfo: units[i].Dies()) {
                DwarfDieWrapper die = dwarfContext.GetDIEForOffset(dieInfo.GetOffset());
                switch (die.GetTag()) {
                    case dwarf::DW_TAG_subprogram: {
                        if (!options_.importFunctions) {
                            break;
                        }

                        auto info = FunctionDecoder{context, die}.Decode();
                        if (!info) {
                            break;
                        }
                        auto [_, ok] = importedFunctions.insert(info->entryPoint);
                        if (!ok) {
                            continue;
                        }

                        QualifiedName name = nameIndex.DecodeQualifiedName(die);
                        DebugFunctionInfo symbol{
                            name.back(),
                            name.GetString(),
                            fmt::format("sub_{:#016x}", info->entryPoint),
                            info->entryPoint,
                            info->type,
                            binaryView_.GetDefaultPlatform()};
                        symbol.type = info->type;

                        debugInfo_.AddFunction(symbol);
                        break;
                    }
                    case dwarf::DW_TAG_constant:
                    case dwarf::DW_TAG_variable: {
                        if (!options_.importGlobals) {
                            break;
                        }
                        auto info = VariableDecoder{context, die}.Decode();
                        if (!info) {
                            break;
                        }

                        auto [_, ok] = importedGlobals.insert(info->location);
                        if (!ok) {
                            continue;
                        }

                        Ref<Symbol> symbol = new Symbol{
                            BNSymbolType::DataSymbol,
                            info->qualifiedName.back(),
                            info->qualifiedName.GetString(),
                            fmt::format("data_{:#016x}", info->location),
                            info->location,
                        };
                        debugInfo_.AddDataVariable(info->location, info->type, info->qualifiedName.GetString());
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
            monitor_(DwarfImportPhase::ImportingFunctionsAndGlobals, i, numUnits);
        }

        BDLogInfo("imported {} functions", importedFunctions.size());
        BDLogInfo("imported {} globals", importedGlobals.size());
    }
}

bool DwarfImportTask::IsNamedTypeTag(dwarf::Tag tag) {
    switch (tag) {
        case dwarf::DW_TAG_typedef:
        case dwarf::DW_TAG_array_type:
        case dwarf::DW_TAG_enumeration_type:
        case dwarf::DW_TAG_subroutine_type:
        case dwarf::DW_TAG_structure_type:
        case dwarf::DW_TAG_union_type:
        case dwarf::DW_TAG_class_type:
        case dwarf::DW_TAG_ptr_to_member_type:
        case dwarf::DW_TAG_unspecified_type:
            return true;
        default:
            return false;
    }
}

DwarfContextWrapper DwarfImportTask::BuildDwarfContext() {
    auto targetObjects = MachO::MachBinaryView{binaryView_}.ReadMachOHeaders();
    std::vector<DwarfContextWrapper::Entry> entries;
    for (const auto &sourceObject: dwarfObjects_) {
        DwarfObjectFile object{sourceObject};
        auto uuid = object.DecodeUUID();
        BDVerify(uuid);
        BDVerify(targetObjects.contains(*uuid));
        auto symbolSegments = object.DecodeSegments();
        entries.emplace_back(DwarfContextWrapper::Entry{
            .object = std::move(object),
            .slider = AddressSlider::CreateFromMachOSegments(
                symbolSegments, targetObjects[*uuid])});
    }
    return DwarfContextWrapper{std::move(entries)};
}
