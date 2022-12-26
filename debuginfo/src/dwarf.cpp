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


#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFExpression.h>
#include <llvm/DebugInfo/DWARF/DWARFUnit.h>

#include <fmt/format.h>

#include <binja/utils/log.h>

#include "debug.h"
#include "dwarf.h"

using namespace llvm;
using namespace Binja;
using namespace DebugInfo;

using dwarf::Attribute;


/// LLVM error utility methods

namespace {

std::string LLVMErrorToString(llvm::Error error) {
    std::string out;
    llvm::raw_string_ostream ss{out};
    logAllUnhandledErrors(std::move(error), ss, "");
    return out;
}

std::string LLVMFormValueToString(const Optional<DWARFFormValue> &value, const char *defaultValue) {
    if (value) {
        Expected<const char *> cstr = value->getAsCString();
        if (cstr) {
            return std::string(cstr.get());
        }
        LLVMErrorToString(cstr.takeError());
    }
    return std::string(defaultValue);
}

}// namespace


/// Dwarf unit wrapper

std::vector<DwarfDebugInfoEntryWrapper> DwarfUnitWrapper::Dies() const {
    std::vector<DwarfDebugInfoEntryWrapper> entries;
    for (const auto &die: unit_.dies()) {
        entries.push_back(DwarfDebugInfoEntryWrapper{die, binaryId_});
    }
    return entries;
}

uint8_t DwarfUnitWrapper::GetAddressByteSize() const {
    return unit_.getAddressByteSize();
}

const dwarf::FormParams DwarfUnitWrapper::GetFormParams() {
    return unit_.getFormParams();
}


/// Dwarf die wrapper

Optional<DwarfDieWrapper> DwarfDieWrapper::GetAttributeValueAsReferencedDie(const llvm::DWARFFormValue &value) const {
    if (auto ref = die_.getAttributeValueAsReferencedDie(value)) {
        return DwarfDieWrapper{ref, (BinaryId) offset_.binaryId};
    }
    return llvm::None;
}

DwarfUnitWrapper DwarfDieWrapper::GetDwarfUnit() const {
    return DwarfUnitWrapper{*die_.getDwarfUnit(), (BinaryId) offset_.binaryId};
}

void DwarfDieWrapper::Dump(raw_ostream &ostream, uint32_t indent, llvm::DIDumpOptions opts) {
    die_.dump(ostream, indent, opts);
}

DwarfDieWrapper DwarfDieWrapper::GetParent() const {
    return DwarfDieWrapper{die_.getParent(), (BinaryId) offset_.binaryId};
}

DwarfDieWrapper DwarfDieWrapper::GetSibling() {
    return DwarfDieWrapper{die_.getSibling(), (BinaryId) offset_.binaryId};
}

DwarfDieWrapper DwarfDieWrapper::GetPreviousSibling() {
    return DwarfDieWrapper{die_.getPreviousSibling(), (BinaryId) offset_.binaryId};
}

bool DwarfDieWrapper::operator==(const DwarfDieWrapper &oth) const {
    return die_ == oth.die_ && offset_.binaryId == oth.offset_.binaryId;
}

DwarfDieWrapper::Iterator DwarfDieWrapper::Children() {
    return llvm::iterator_range<Detail::DwarfDieWrapperIterator>(
        Detail::DwarfDieWrapperIterator{GetFirstChild()},
        Detail::DwarfDieWrapperIterator{GetLastChild()});
}

DwarfDieWrapper DwarfDieWrapper::GetFirstChild() {
    return DwarfDieWrapper{die_.getFirstChild(), (BinaryId) offset_.binaryId};
}

DwarfDieWrapper DwarfDieWrapper::GetLastChild() {
    return DwarfDieWrapper{die_.getLastChild(), (BinaryId) offset_.binaryId};
}

Expected<DWARFAddressRangesVector> DwarfDieWrapper::GetAddressRanges() {
    return die_.getAddressRanges();
}

Expected<DWARFLocationExpressionsVector> DwarfDieWrapper::GetLocations(llvm::dwarf::Attribute attr) {
    return die_.getLocations(attr);
}


/// Dwarf context wrapper

DwarfDieWrapper DwarfContextWrapper::GetDIEForOffset(DwarfOffset offset) {
    DWARFDie die = entries_[offset.binaryId].object.GetDWARFContext().getDIEForOffset(offset.offset);
    return DwarfDieWrapper{die, (BinaryId) offset.binaryId};
}

std::vector<DwarfUnitWrapper> DwarfContextWrapper::GetNormalUnitsVector() {
    std::vector<DwarfUnitWrapper> result;
    for (size_t i = 0; i < entries_.size(); ++i) {
        llvm::DWARFContext &ctx = entries_[i].object.GetDWARFContext();
        for (const auto &unit: ctx.getNormalUnitsVector()) {
            result.push_back(DwarfUnitWrapper{*unit, (BinaryId) i});
        }
    }
    return result;
}

std::optional<uint64_t> DwarfContextWrapper::GetSlidAddress(DwarfOffset offset, uint64_t address) {
    if (auto value = entries_[offset.binaryId].slider.SlideAddress(address)) {
        return value;
    }
    return std::nullopt;
}


/// Attribute reader

std::optional<uint64_t> AttributeReader::ReadUInt(Attribute attribute, bool recursive) const {
    if (auto ref = FindAttribute(attribute, recursive)) {
        if (auto value = ref->getAsUnsignedConstant()) {
            return *value;
        }
    }
    return std::nullopt;
}

std::optional<uint64_t> AttributeReader::ReadInt(Attribute attribute, bool recursive) const {
    if (auto ref = FindAttribute(attribute, recursive)) {
        if (auto value = ref->getAsSignedConstant()) {
            return *value;
        }
    }
    return std::nullopt;
}

std::string AttributeReader::ReadString(Attribute attribute, const char *defaultValue, bool recursive) const {
    if (recursive) {
        return LLVMFormValueToString(die_.FindRecursively(attribute), defaultValue);
    }
    return LLVMFormValueToString(die_.Find(attribute), defaultValue);
}

std::optional<DwarfDieWrapper> AttributeReader::ReadReference(Attribute attribute, bool recursive) const {
    if (auto attr = FindAttribute(attribute, recursive)) {
        return die_.GetAttributeValueAsReferencedDie(*attr).getValue();
    }
    return std::nullopt;
}

std::string AttributeReader::ReadName(const char *defaultName, bool recursive) const {
    return ReadString(llvm::dwarf::DW_AT_name, defaultName, recursive);
}

bool AttributeReader::HasAttribute(Attribute attribute, bool recursive) const {
    if (auto ref = FindAttribute(attribute, recursive)) {
        return true;
    }
    return false;
}

std::string AttributeReader::ReadLinkageName(const char *defaultName, bool recursive) const {
    return ReadString(dwarf::DW_AT_linkage_name, defaultName, recursive);
}

std::optional<llvm::DWARFFormValue> AttributeReader::FindAttribute(
    llvm::dwarf::Attribute attr, bool recursive) const {
    if (recursive) {
        if (auto v = die_.FindRecursively(attr)) {
            return *v;
        }
    } else {
        if (auto v = die_.Find(attr)) {
            return *v;
        }
    }
    return std::nullopt;
}

std::optional<uint64_t> AttributeReader::ReadLocationAddress() const {
    auto formValue = die_.Find(dwarf::DW_AT_location);
    if (!formValue) {
        return std::nullopt;
    }

    auto block = formValue->getAsBlock();
    if (!block) {
        return std::nullopt;
    }

    auto unit = die_.GetDwarfUnit();

    // TODO: hardcoded endian
    DataExtractor data(StringRef((const char *) block->data(), block->size()), true, 0);
    DWARFExpression expression{data, unit.GetAddressByteSize(), unit.GetFormParams().Format};
    if (expression.begin() == expression.end()) {
        return std::nullopt;
    }

    auto operation = *expression.begin();
    if (operation.getCode() != dwarf::DW_OP_addr) {
        return std::nullopt;
    }
    return operation.getRawOperand(0);
}

/// DIE reader


std::string DieReader::Dump() const {
    auto addressSize = die_.GetDwarfUnit().GetAddressByteSize();
    std::string out;
    llvm::raw_string_ostream ss{out};
    llvm::DIDumpOptions opts{.AddrSize = addressSize};
    ss << "\n";
    ss << "=========================\n"
          "PARENTS: \n"
          "=========================\n";
    opts.ShowParents = true;
    opts.ShowAddresses = true;
    opts.ShowForm = true;
    die_.Dump(ss, 0, opts);
    ss << "\n";
    ss << "=========================\n"
          "CHILDREN: \n"
          "=========================\n";
    opts.ShowParents = false;
    opts.ShowChildren = true;
    die_.Dump(ss, 0, opts);
    return out;
}

namespace {

class QualifiedNameBuilder {
public:
    explicit QualifiedNameBuilder(DwarfDieWrapper &die) : die_{die} {}

    std::vector<std::string> Build() {
        using namespace dwarf;
        auto tag = die_.GetTag();
        AttributeReader reader{die_};
        switch (tag) {
            case DW_TAG_variable:
            case DW_TAG_array_type:
            case DW_TAG_base_type:
            case DW_TAG_subroutine_type:
            case DW_TAG_unspecified_type: {
                auto name = reader.ReadString(DW_AT_name);
                if (name.empty()) {
                    name = GetAnonymousName(die_);
                }
                qf_.push_back(name);
                DwarfDieWrapper parent = die_.GetParent();
                ScanContainer(parent);
                break;
            }
            default: {
                ScanContainer(die_);
                break;
            }
        }
        std::reverse(qf_.begin(), qf_.end());
        return std::move(qf_);
    }

private:
    void ScanContainer(DwarfDieWrapper &die) {
        using namespace dwarf;

        if (!die.IsValid()) {
            return;
        }

        auto tag = die.GetTag();
        AttributeReader reader{die};
        std::string name = reader.ReadString(DW_AT_name, "", true);

        switch (tag) {
            case llvm::dwarf::DW_TAG_compile_unit:
                return;
            case DW_TAG_namespace: {
                if (name.empty()) {
                    name = GetAnonymousName(die);
                }
                qf_.push_back(name);
                break;
            }
            case DW_TAG_lexical_block: {
                name = GetAnonymousName(die);
                qf_.push_back(name);
                break;
            }
            case DW_TAG_enumeration_type: {
                if (!reader.HasAttribute(DW_AT_enum_class)) {
                    break;
                }
                // fallthrough
            }
            case DW_TAG_base_type:
            case DW_TAG_typedef:
            case DW_TAG_template_alias: {
                VerifyDebugDumpDie(!name.empty(), die);
                if (name.empty()) {
                    name = GetAnonymousName(die);
                }
                qf_.push_back(name);
                break;
            }
            case DW_TAG_class_type: {
                if (auto base = reader.ReadReference(DW_AT_specification)) {
                    ScanContainer(*base);
                    return;
                }
                // fallthrough
            }
            case DW_TAG_structure_type:
            case DW_TAG_union_type: {
                if (!reader.HasAttribute(DW_AT_export_symbols)) {
                    if (name.empty()) {
                        name = GetAnonymousName(die);
                    }
                    qf_.push_back(name);
                }
                break;
            }
            case DW_TAG_inlined_subroutine: {
                auto base = reader.ReadReference(DW_AT_abstract_origin);
                VerifyDumpDie(base, die);
                ScanContainer(*base);
                break;
            }
            case DW_TAG_subprogram: {
                if (auto base = reader.ReadReference(DW_AT_specification)) {
                    ScanContainer(*base);
                    return;
                }
                if (auto base = reader.ReadReference(DW_AT_abstract_origin)) {
                    ScanContainer(*base);
                    return;
                }
                if (name.empty()) {
                    name = GetAnonymousName(die);
                }
                qf_.push_back(name);
                break;
            }
            default: {
                throw DwarfError{"unexpected container type {}, DIE: {}",
                                 TagString(tag), DieReader{die}.Dump()};
            }
        }

        DwarfDieWrapper parent = die.GetParent();
        ScanContainer(parent);
    }

    static const char *GetAnonymousNameSuffix(dwarf::Tag tag) {
        switch (tag) {
            case dwarf::DW_TAG_namespace:
                return "ns";
            case dwarf::DW_TAG_structure_type:
                return "struct";
            case dwarf::DW_TAG_class_type:
                return "class";
            case dwarf::DW_TAG_union_type:
                return "union";
            case dwarf::DW_TAG_subprogram:
            case dwarf::DW_TAG_inlined_subroutine:
                return "function";
            case dwarf::DW_TAG_subroutine_type:
                return "functor";
            case dwarf::DW_TAG_enumeration_type:
                return "enum";
            case dwarf::DW_TAG_lexical_block:
                return "block";
            default:
                throw FatalError{"unexpected dwarf tag {}", dwarf::TagString(tag)};
        }
    }

    static std::string GetAnonymousName(DwarfDieWrapper &die) {
        return fmt::format("__anon_{}_{:#04x}_{:#08x}", GetAnonymousNameSuffix(die.GetTag()),
                           die.GetOffset().binaryId, die.GetOffset().offset);
    }

private:
    DwarfDieWrapper &die_;
    std::vector<std::string> qf_;
};

}// namespace

std::vector<std::string> DieReader::ReadQualifiedName() const {
    return QualifiedNameBuilder{die_}.Build();
}

uint8_t DieReader::ReadAddressSize() const {
    return die_.GetDwarfUnit().GetAddressByteSize();
}
