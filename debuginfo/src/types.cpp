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


#include <unordered_set>

#include <llvm/DebugInfo/DWARF/DWARFUnit.h>

#include <binja/utils/log.h>

#include "debug.h"
#include "types.h"

namespace BN = BinaryNinja;
namespace DW = llvm::dwarf;
using namespace Binja;
using namespace DebugInfo;

using BN::Type;
using TypeRef = BN::Ref<Type>;
using BN::QualifiedName;

const DW::Tag DW_TAG_APPLE_ptrauth_type = (DW::Tag) 0x4300;// NOLINT(readability-identifier-naming)


/// Type builder

bool TypeBuilder::IsTypeTag(DW::Tag tag) {
    static const std::unordered_set<DW::Tag> kDwarfTypeTags{
        DW::DW_TAG_array_type,
        DW::DW_TAG_class_type,
        DW::DW_TAG_enumeration_type,
        DW::DW_TAG_pointer_type,
        DW::DW_TAG_reference_type,
        DW::DW_TAG_string_type,
        DW::DW_TAG_structure_type,
        DW::DW_TAG_subroutine_type,
        DW::DW_TAG_typedef,
        DW::DW_TAG_union_type,
        DW::DW_TAG_ptr_to_member_type,
        DW::DW_TAG_set_type,
        DW::DW_TAG_subrange_type,
        DW::DW_TAG_base_type,
        DW::DW_TAG_const_type,
        DW::DW_TAG_file_type,
        DW::DW_TAG_packed_type,
        DW::DW_TAG_thrown_type,
        DW::DW_TAG_volatile_type,
        DW::DW_TAG_restrict_type,
        DW::DW_TAG_interface_type,
        DW::DW_TAG_unspecified_type,
        DW::DW_TAG_shared_type,
        DW::DW_TAG_rvalue_reference_type,
        DW::DW_TAG_coarray_type,
        DW::DW_TAG_dynamic_type,
        DW::DW_TAG_atomic_type,
        DW::DW_TAG_immutable_type,
        DW_TAG_APPLE_ptrauth_type,
    };
    return kDwarfTypeTags.contains(tag);
}

/// Base type builder

BN::Ref<BinaryNinja::Type> BaseTypeBuilder::Build() {
    DebugVerify(die_.GetTag() == DW::DW_TAG_base_type, FatalError);

    std::optional<uint64_t> encoding = attributeReader_.ReadUInt(DW::DW_AT_encoding);
    VerifyDumpDie(encoding, die_);

    std::optional<uint64_t> size = attributeReader_.ReadUInt(DW::DW_AT_byte_size);
    VerifyDumpDie(size, die_);

    std::vector<std::string> qualifiedName = dieReader_.ReadQualifiedName();
    VerifyDumpDie(qualifiedName.size() == 1, die_);
    return MapBNType(*size, *encoding);
}

BinaryNinja::Ref<BinaryNinja::Type> BaseTypeBuilder::MapBNType(uint64_t size, uint64_t encoding) {
    switch (encoding) {
        case DW::DW_ATE_boolean:
            return Type::BoolType();
        case DW::DW_ATE_address:
            return Type::PointerType(size, Type::VoidType());
        case DW::DW_ATE_signed:
        case DW::DW_ATE_signed_char:
            return Type::IntegerType(size, true);
        case DW::DW_ATE_unsigned:
        case DW::DW_ATE_unsigned_char:
            return Type::IntegerType(size, false);
        case DW::DW_ATE_UTF: {
            switch (size) {
                case 1:
                    return Type::IntegerType(1, true);
                case 2:
                    return Type::WideCharType(2, "char16_t");
                default:
                    return Type::WideCharType(size);
            }
            case DW::DW_ATE_float:
            case DW::DW_ATE_decimal_float:
                return Type::FloatType(size);
            case DW::DW_ATE_ASCII:
            case DW::DW_ATE_UCS:
            case DW::DW_ATE_signed_fixed:
            case DW::DW_ATE_unsigned_fixed:
            case DW::DW_ATE_complex_float:
            case DW::DW_ATE_imaginary_float:
            case DW::DW_ATE_packed_decimal:
            case DW::DW_ATE_numeric_string:
            case DW::DW_ATE_edited:
                throw DwarfError{"base type encoding {} not supported for DIE {}",
                                 encoding, dieReader_.Dump()};
            default:
                throw DwarfError{"invalid base type encoding {} for DIE {}",
                                 encoding, dieReader_.Dump()};
        }
    }
}


/// Type modifier builder

BinaryNinja::Ref<BinaryNinja::Type> TypeModifierBuilder::Build() {
    auto tag = die_.GetTag();
    Verify(IsTypeModifierTag(tag), FatalError);

    auto base = attributeReader_.ReadReference(DW::DW_AT_type);

    BN::Ref<Type> baseType;
    if (base) {
        baseType = GenericTypeBuilder{ctx_, *base}.Build();
    } else {
        baseType = Type::VoidType();
    }

    BN::TypeBuilder builder{baseType};

    switch (tag) {
        case DW::DW_TAG_const_type:
            builder.SetConst(true);
            break;
        case DW::DW_TAG_volatile_type:
            builder.SetVolatile(true);
            break;
        case DW::DW_TAG_pointer_type:
            return Type::PointerType(dieReader_.ReadAddressSize(), baseType);
        case DW::DW_TAG_reference_type:
            return Type::PointerType(dieReader_.ReadAddressSize(), baseType,
                                     false, false, BNReferenceType::ReferenceReferenceType);
        case DW::DW_TAG_rvalue_reference_type:
            return Type::PointerType(dieReader_.ReadAddressSize(), baseType,
                                     false, false, BNReferenceType::ReferenceReferenceType);
        case DW::DW_TAG_packed_type:
            if (baseType->IsStructure()) {
                BN::StructureBuilder structureBuilder{baseType->GetStructure()};
                structureBuilder.SetPacked(true);
                return Type::StructureType(structureBuilder.Finalize());
            }
            BDLogWarn("attempt to apply packed modifier on non struct type {}, DIE: {}",
                      baseType->GetTypeName().GetString(), dieReader_.Dump());
            return baseType;
        case DW_TAG_APPLE_ptrauth_type:
            return baseType;
        case DW::DW_TAG_atomic_type:
        case DW::DW_TAG_immutable_type:
        case DW::DW_TAG_restrict_type:
        case DW::DW_TAG_shared_type:
            BDLogWarn("encountered unsupported type modifier tag {}", DW::TagString(tag));
            return baseType;
        default:
            BDLogWarn("encountered unknown type modifier tag {}", DW::TagString(tag));
            return baseType;
    }

    return builder.Finalize();
}


bool TypeModifierBuilder::IsTypeModifierTag(DW::Tag tag) {
    static const std::unordered_set<DW::Tag> kTypeModifierTags{
        DW::DW_TAG_const_type,
        DW::DW_TAG_volatile_type,
        DW::DW_TAG_pointer_type,
        DW::DW_TAG_reference_type,
        DW::DW_TAG_rvalue_reference_type,
        DW::DW_TAG_atomic_type,
        DW::DW_TAG_immutable_type,
        DW::DW_TAG_packed_type,
        DW::DW_TAG_restrict_type,
        DW::DW_TAG_shared_type,
        DW_TAG_APPLE_ptrauth_type,
    };
    return kTypeModifierTags.contains(tag);
}


/// Typedef builder

BinaryNinja::Ref<BinaryNinja::Type> TypedefBuilder::Build() {
    auto base = attributeReader_.ReadReference(DW::DW_AT_type);
    if (!base) {
        BDLogWarn("typedef without DW_AT_type attribute, DIE: {}", dieReader_.Dump());
        return nullptr;
    }

    auto name = attributeReader_.ReadName();
    if (name.empty()) {
        BDLogWarn("typedef without DW_AT_name attribute, DIE: {}", dieReader_.Dump());
        return nullptr;
    }

    BN::Ref<Type> baseType = GenericTypeBuilder{ctx_, *base}.Build();
    return baseType;
}

std::optional<DwarfDieWrapper> TypedefBuilder::Resolve(DwarfDieWrapper &die) {
    auto type = AttributeReader{die}.ReadReference(DW::DW_AT_type);
    while (type) {
        if (type->GetTag() != DW::DW_TAG_typedef) {
            return *type;
        }
        type = AttributeReader{*type}.ReadReference(DW::DW_AT_type);
    }
    return std::nullopt;
}


/// Array type builder

BinaryNinja::Ref<BinaryNinja::Type> ArrayTypeBuilder::Build() {
    auto name = attributeReader_.ReadName();
    if (!name.empty()) {
        BDLogWarn("ignoring array with DW_AT_name not implemented, DIE: {}", dieReader_.Dump());
        return nullptr;
    }

    auto elementType = attributeReader_.ReadReference(DW::DW_AT_type);
    if (!elementType) {
        BDLogWarn("ignoring array with no DW_AT_type attribute, DIE: {}", dieReader_.Dump());
        return nullptr;
    }

    if (attributeReader_.HasAttribute(DW::DW_AT_rank)) {
        return BuildDynamic();
    }

    return BuildStatic();
}

BinaryNinja::Ref<BinaryNinja::Type> ArrayTypeBuilder::BuildDynamic() {
    auto rank = attributeReader_.ReadUInt(DW::DW_AT_rank);
    if (!rank) {
        BDLogWarn("ignoring array having DW_AT_rank value as DWARF expression, DIE: {}",
                  dieReader_.Dump());
        return nullptr;
    }

    if (*rank == 0) {
        BDLogWarn("ignoring array having DW_AT_rank value 0, DIE: {}",
                  dieReader_.Dump());
        return nullptr;
    }

    auto elementType = *attributeReader_.ReadReference(DW::DW_AT_type);

    BN::Ref<Type> result = GenericTypeBuilder{ctx_, elementType}.Build();
    for (int i = 0; i < *rank; ++i) {
        result = Type::PointerType(dieReader_.ReadAddressSize(), result);
    }

    return result;
}

BinaryNinja::Ref<BinaryNinja::Type> ArrayTypeBuilder::BuildStatic() {
    std::vector<size_t> dimensions;
    for (auto &child: die_.Children()) {
        if (auto dim = DecodeCountFromSubrange(const_cast<DwarfDieWrapper &>(child))) {
            dimensions.push_back(*dim);
        } else {
            dimensions.push_back(0);
        }
    }

    auto elementType = *attributeReader_.ReadReference(DW::DW_AT_type);
    BN::Ref<Type> result = GenericTypeBuilder{ctx_, elementType}.Build();

    for (auto it = dimensions.rbegin(), end = dimensions.rend(); it != end; ++it) {
        if (*it != 0) {
            result = Type::ArrayType(result, *it);
        } else {
            result = Type::PointerType(dieReader_.ReadAddressSize(), result);
        }
    }

    return result;
}


std::optional<size_t> ArrayTypeBuilder::DecodeCountFromSubrange(DwarfDieWrapper &die) {
    AttributeReader attributeReader{die};
    if (auto count = attributeReader.ReadUInt(DW::DW_AT_count)) {
        return count;
    }

    if (auto ub = attributeReader.ReadUInt(DW::DW_AT_upper_bound)) {
        size_t lb;
        if (auto value = attributeReader.ReadUInt(DW::DW_AT_lower_bound)) {
            lb = *value;
        } else {
            lb = GetDefaultLowerBound();
        }
        if (*ub <= lb) {
            BDLogWarn("ignoring array index with ub <= lb, die: {}", dieReader_.Dump());
            return std::nullopt;
        }
        return *ub - lb;
    }

    return std::nullopt;
}

size_t ArrayTypeBuilder::GetDefaultLowerBound() {
    // TODO: language dependent???
    return 0;
}


/// Function type builder

BinaryNinja::Ref<BinaryNinja::Type> FunctionTypeBuilder::Build() {
    auto returnType = DecodeReturnType();
    auto parameters = DecodeParameters();
    return Type::FunctionType(returnType, DecodeCallingConvention(),
                              parameters.parameters, parameters.hasVarArg);
}

BinaryNinja::Ref<BinaryNinja::Type> FunctionTypeBuilder::DecodeReturnType() {
    auto returnType = attributeReader_.ReadReference(DW::DW_AT_type, true);
    if (!returnType) {
        return Type::VoidType();
    }
    return GenericTypeBuilder{ctx_, *returnType}.Build();
}

BinaryNinja::Ref<BinaryNinja::CallingConvention> FunctionTypeBuilder::DecodeCallingConvention() {
    // TODO??
    return nullptr;
}

FunctionTypeBuilder::DecodeParametersResult FunctionTypeBuilder::DecodeParameters() {
    DecodeParametersResult result{.hasVarArg = false};
    for (auto &entry: die_.Children()) {
        auto tag = entry.GetTag();
        auto &child = const_cast<DwarfDieWrapper &>(entry);
        AttributeReader attributeReader{child};
        switch (tag) {
            case DW::DW_TAG_formal_parameter: {
                if (result.hasVarArg) {
                    BDLogWarn("encountered function with formal parameter "
                              "after vararg, DIE: {}",
                              dieReader_.Dump());
                }

                BN::FunctionParameter functionParameter;
                functionParameter.type = ApplyParameterTypeModifiers(DecodeParameterType(child), child);
                functionParameter.name = attributeReader.ReadName("", true);
                functionParameter.defaultLocation = true;
                result.parameters.push_back(std::move(functionParameter));
                break;
            }
            case DW::DW_TAG_unspecified_parameters: {
                result.hasVarArg = true;
                break;
            }
            default: {
                break;
            }
        }
    }
    return result;
}

BinaryNinja::Ref<BinaryNinja::Type> FunctionTypeBuilder::DecodeParameterType(DwarfDieWrapper &die) {
    AttributeReader attributeReader{die};
    auto type = attributeReader.ReadReference(DW::DW_AT_type, true);
    if (!type) {
        BDLogWarn("encountered function formal parameter with no DW_AT_type "
                  "attribute, DIE: {}",
                  DieReader{die}.Dump());
        return Type::VoidType();
    }
    return GenericTypeBuilder{ctx_, *type}.Build();
}

BinaryNinja::Ref<BinaryNinja::Type> FunctionTypeBuilder::ApplyParameterTypeModifiers(
    BinaryNinja::Ref<BinaryNinja::Type> type, DwarfDieWrapper &die) {
    AttributeReader attributeReader{die};
    DieReader dieReader{die};
    bool isReferenceType = attributeReader.HasAttribute(DW::DW_AT_reference, true);
    bool isRValueReferenceType = attributeReader.HasAttribute(DW::DW_AT_rvalue_reference, true);
    if (isRValueReferenceType && isReferenceType) {
        BDLogWarn("function parameter have both DW_AT_reference and DW_AT_rvalue_reference "
                  "tags, DIE: {}",
                  DieReader{die}.Dump());
        return type;
    }
    if (isRValueReferenceType) {
        return Type::PointerType(dieReader.ReadAddressSize(), type, false, false,
                                 BNReferenceType::RValueReferenceType);
    }
    if (isReferenceType) {
        return Type::PointerType(dieReader.ReadAddressSize(), type, false, false,
                                 BNReferenceType::ReferenceReferenceType);
    }
    return type;
}


/// Enum type builder

BinaryNinja::Ref<BinaryNinja::Type> EnumTypeBuilder::Build() {
    auto type = ResolveBaseType();
    if (!type) {
        BDLogWarn("ignoring enum with no / invalid DW_AT_type attribute, DIE: {}", dieReader_.Dump());
        return nullptr;
    }

    if (type->GetTag() != DW::DW_TAG_base_type) {
        BDLogWarn("ignoring enum having base type with tag != DW_TAG_base_type, DIE: {}",
                  dieReader_.Dump());
        return nullptr;
    }

    BN::Ref<Type> baseType = GenericTypeBuilder{ctx_, *type}.Build();
    auto size = attributeReader_.ReadUInt(DW::DW_AT_byte_size);
    if (!size) {
        size = baseType->GetWidth();
    }

    auto isClass = attributeReader_.HasAttribute(DW::DW_AT_enum_class);
    if (isClass) {
        BDLogDebug("encountered class enum {}", dieReader_.Dump());
    }

    BN::EnumerationBuilder builder{};
    for (auto &child: die_.Children()) {
        auto tag = child.GetTag();
        if (tag == DW::DW_TAG_enumerator) {
            auto &enumerator = const_cast<DwarfDieWrapper &>(child);
            AttributeReader attributeReader{enumerator};
            std::string name = attributeReader.ReadName();
            if (name.empty()) {
                BDLogWarn("ignoring enum entry with no name, DIE: {}", DieReader{enumerator}.Dump());
                continue;
            }
            if (baseType->IsSigned()) {
                auto value = attributeReader.ReadInt(DW::DW_AT_const_value);
                if (!value) {
                    BDLogWarn("ignoring enum entry with no value, DIE: {}", DieReader{enumerator}.Dump());
                    continue;
                }
                builder.AddMemberWithValue(name, *value);
            } else {
                auto value = attributeReader.ReadUInt(DW::DW_AT_const_value);
                if (!value) {
                    BDLogWarn("ignoring enum entry with no value, DIE: {}", DieReader{enumerator}.Dump());
                    continue;
                }
                builder.AddMemberWithValue(name, *value);
            }
        } else {
            BDLogWarn("ignoring unexpected tag {} inside enum, DIE: {}",
                      DW::TagString(tag), dieReader_.Dump());
        }
    }

    return Type::EnumerationType(builder.Finalize(), *size, baseType->IsSigned());
}

std::optional<DwarfDieWrapper> EnumTypeBuilder::ResolveBaseType() {
    auto type = attributeReader_.ReadReference(DW::DW_AT_type);
    while (type) {
        if (type->GetTag() == DW::DW_TAG_base_type) {
            return *type;
        }
        type = AttributeReader{*type}.ReadReference(DW::DW_AT_type);
    }
    return std::nullopt;
}


/// Composite type builder

BinaryNinja::Ref<BinaryNinja::Type> CompositeTypeBuilder::Build() {
    BN::StructureBuilder builder{};
    builder.SetStructureType(DecodeVariant());
    builder.SetPacked(IsPacked());
    builder.SetAlignment(DecodeAlignment());
    builder.SetWidth(DecodeWidth());

    for (auto &child: die_.Children()) {
        switch (child.GetTag()) {
            case DW::DW_TAG_inheritance:
            case DW::DW_TAG_member:
                if (auto result = DecodeMember(const_cast<DwarfDieWrapper &>(child))) {
                    builder.AddMemberAtOffset(result->type, result->name,
                                              result->offset, false, result->access);
                }
                break;
            case DW::DW_TAG_variable:
                if (auto result = DecodeVariable(const_cast<DwarfDieWrapper &>(child))) {
                    builder.AddMember(result->type, result->name, result->access,
                                      BNMemberScope::StaticScope);
                }
                break;
            case DW::DW_TAG_subprogram:
                // TODO: should we add entry in container???
                break;
            case DW::DW_TAG_template_value_parameter:
            case DW::DW_TAG_template_type_parameter:
                break;
            case DW::DW_TAG_structure_type:
            case DW::DW_TAG_union_type:
            case DW::DW_TAG_class_type:
            case DW::DW_TAG_enumeration_type:
            case DW::DW_TAG_typedef:
                // Already handled in member access / index DB iteration
                break;
            default: {
                BDLogInfo("Ignoring unexpected tag {} of DIE {}", DW::TagString(child.GetTag()),
                          DieReader{const_cast<DwarfDieWrapper &>(child)}.Dump());
                break;
            }
        }
    }

    ProcessBitfields(builder);
    return Type::StructureType(builder.Finalize());
}

bool CompositeTypeBuilder::IsCompositeTypeTag(DW::Tag tag) {
    switch (tag) {
        case DW::DW_TAG_structure_type:
        case DW::DW_TAG_union_type:
        case DW::DW_TAG_class_type:
            return true;
        default:
            return false;
    }
}

BNStructureVariant CompositeTypeBuilder::DecodeVariant() {
    switch (die_.GetTag()) {
        case DW::DW_TAG_structure_type:
            return BNStructureVariant::StructStructureType;
        case DW::DW_TAG_union_type:
            return BNStructureVariant::UnionStructureType;
        case DW::DW_TAG_class_type:
            return BNStructureVariant::ClassStructureType;
        default:
            VerifyNotReachable();
    }
}

bool CompositeTypeBuilder::IsPacked() {
    return false;
}

uint8_t CompositeTypeBuilder::DecodeAlignment() {
    return 1;
}

size_t CompositeTypeBuilder::DecodeWidth() {
    if (auto size = attributeReader_.ReadUInt(DW::DW_AT_byte_size)) {
        return *size;
    }
    auto isDeclaration = attributeReader_.HasAttribute(DW::DW_AT_declaration);
    if (!isDeclaration) {
        BDLogWarn("Container does not have DW_AT_byte_size attribute, DIE: {}", dieReader_.Dump());
    }
    return 0;
}

BNMemberAccess CompositeTypeBuilder::GetDefaultMemberAccess() {
    switch (die_.GetTag()) {
        case DW::DW_TAG_structure_type:
        case DW::DW_TAG_union_type:
            return BNMemberAccess::PublicAccess;
        case DW::DW_TAG_class_type:
            return BNMemberAccess::PrivateAccess;
        default:
            VerifyNotReachable();
    }
}

std::optional<CompositeTypeBuilder::DecodeMemberResult> CompositeTypeBuilder::DecodeMember(DwarfDieWrapper &die) {
    DieReader dieReader{die};
    const auto &attributeReader = dieReader.AttrReader();

    if (attributeReader.HasAttribute(DW::DW_AT_data_bit_offset)) {
        return std::nullopt;
    }

    auto type = attributeReader.ReadReference(DW::DW_AT_type);
    if (!type) {
        BDLogInfo("Skipping member DIE without DW_AT_type attribute, "
                  "DIE: {}",
                  dieReader.Dump());
        return std::nullopt;
    }

    auto isExternal = attributeReader.HasAttribute(DW::DW_AT_external);
    if (isExternal) {
        // TODO: static members
        return std::nullopt;
    }

    auto offset = attributeReader.ReadUInt(DW::DW_AT_data_member_location);
    if (!offset) {
        BDLogWarn("composite type member without DW_AT_data_member_location, DIE: {}",
                  DieReader{die}.Dump());
        return std::nullopt;
    }

    std::string name = attributeReader.ReadName("", true);
    bool isAnonymous = name.empty();
    bool isInheritance = die.GetTag() == DW::DW_TAG_inheritance;

    AttributeReader typeAttributeReader{*type};

    if ((isAnonymous && !isInheritance) && !typeAttributeReader.HasAttribute(DW::DW_AT_export_symbols) && !typeAttributeReader.ReadName("").empty()) {
        BDLogDebug("Anonymous member of container does not have DW_AT_export_symbols "
                   "attribute and member type has name, DIE: {}",
                   dieReader.Dump());
    }

    DecodeMemberResult result;
    result.access = DecodeMemberAccess(attributeReader.ReadUInt(DW::DW_AT_accessibility));
    result.name = std::move(name);
    result.offset = *offset;
    result.type = GenericTypeBuilder{ctx_, *type}.Build();

    return result;
}

std::optional<CompositeTypeBuilder::DecodeVariableResult> CompositeTypeBuilder::DecodeVariable(DwarfDieWrapper &die) {
    DieReader dieReader{die};
    const auto &attributeReader = dieReader.AttrReader();

    auto type = attributeReader.ReadReference(DW::DW_AT_type);
    VerifyDebugDumpDie(type, die);

    std::string name = attributeReader.ReadName("", true);
    VerifyDebugDumpDie(!name.empty(), die);

    DecodeVariableResult result;
    result.access = DecodeMemberAccess(attributeReader.ReadUInt(DW::DW_AT_accessibility));
    result.name = std::move(name);
    result.type = GenericTypeBuilder{ctx_, *type}.Build();
    return result;
}


BNMemberAccess CompositeTypeBuilder::DecodeMemberAccess(std::optional<uint64_t> accessibility) {
    if (!accessibility) {
        return GetDefaultMemberAccess();
    }
    switch (*accessibility) {
        case DW::DW_ACCESS_private:
            return BNMemberAccess::PrivateAccess;
        case DW::DW_ACCESS_protected:
            return BNMemberAccess::ProtectedAccess;
        case DW::DW_ACCESS_public:
            return BNMemberAccess::PublicAccess;
    }
    BDLogWarn("encountered struct having member invalid DW_AT_accessibility "
              "value, DIE: {}",
              dieReader_.Dump());
    return NoAccess;
}


void CompositeTypeBuilder::ProcessBitfields(BN::StructureBuilder &builder) {
    auto child = die_.GetFirstChild();
    while (child.IsValid()) {
        if (child.GetTag() == DW::DW_TAG_member) {
            AttributeReader attributeReader{child};
            if (attributeReader.HasAttribute(DW::DW_AT_bit_size)) {
                if (auto next = ProcessBitfield(builder, child)) {
                    child = *next;
                } else {
                    BDLogWarn("failed processing of bitfields in DIE {}", dieReader_.Dump());
                    return;
                }
                continue;
            }
        }
        child = child.GetSibling();
    }
}


std::optional<DwarfDieWrapper> CompositeTypeBuilder::ProcessBitfield(
    BinaryNinja::StructureBuilder &builder, DwarfDieWrapper &start) {
    const int kMaxBitSize = 64;

    int startBit = 0;
    if (auto bit = AttributeReader{start}.ReadUInt(DW::DW_AT_bit_offset)) {
        startBit = *bit;
    }

    if (startBit % 8 != 0) {
        BDLogWarn("unexpected alignment of start bit in DIE offset: {}", start.GetOffset());
        return std::nullopt;
    }

    int bitsUsed = 0;
    DwarfDieWrapper end = start.GetSibling();
    for (int previousMaxBit = startBit; end.IsValid();) {
        if (end.GetTag() != DW::DW_TAG_member) {
            break;
        }

        AttributeReader attributeReader{end};
        auto bitSize = attributeReader.ReadUInt(DW::DW_AT_bit_size);
        if (!bitSize) {
            break;
        }

        auto bitOffset = attributeReader.ReadUInt(DW::DW_AT_data_bit_offset);
        if (!bitOffset) {
            bitOffset = 0;
        }

        int maxBit = *bitOffset + *bitSize;
        if (maxBit < previousMaxBit) {
            BDLogWarn("unexpected order of bitfields in DIE offset: {}", end.GetOffset());
            return std::nullopt;
        }

        if (maxBit - startBit > kMaxBitSize) {
            break;
        }

        bitsUsed = maxBit - startBit;
        end = end.GetSibling();
    }

    BN::EnumerationBuilder enumerationBuilder{};
    int enumSize = bitsUsed <= 8 ? 1 : bitsUsed <= 16 ? 2
                                   : bitsUsed <= 32   ? 4
                                                      : 8;

    for (DwarfDieWrapper die = start; die != end; die = die.GetSibling()) {
        AttributeReader bitfieldAttributeReader{die};

        auto bitSize = *bitfieldAttributeReader.ReadUInt(DW::DW_AT_bit_size);
        auto bitOffset = bitfieldAttributeReader.ReadUInt(DW::DW_AT_data_bit_offset);
        bitOffset = bitOffset ? bitOffset : 0;

        auto name = bitfieldAttributeReader.ReadName();
        if (name.empty()) {
            name = fmt::format("__bitfield_noname_{}", *bitOffset);
        }

        enumerationBuilder.AddMemberWithValue(fmt::format("{}_bit_offset", name), *bitOffset);
        enumerationBuilder.AddMemberWithValue(fmt::format("{}_bit_size", name), bitSize);
    }

    BN::Ref<Type> enumeration = Type::EnumerationType(enumerationBuilder.Finalize(), enumSize, false);
    builder.AddMemberAtOffset(enumeration, "", bitsUsed / 8);
    return end;
}

/// Pointer to member type builder

BinaryNinja::Ref<BinaryNinja::Type> PointerToMemberTypeBuilder::Build() {
    auto memberType = attributeReader_.ReadReference(DW::DW_AT_type);
    if (!memberType) {
        BDLogWarn("encountered pointer to member type with no DW_AT_type, DIE: {}",
                  dieReader_.Dump());
        return nullptr;
    }

    auto containerType = attributeReader_.ReadReference(DW::DW_AT_containing_type);
    if (!containerType) {
        BDLogWarn("encountered pointer to member type with no DW_AT_containing_type, DIE: {}",
                  dieReader_.Dump());
        return nullptr;
    }

    BN::StructureBuilder builder;
    auto memberTypeRef = GenericTypeBuilder{ctx_, *memberType}.Build();
    if (!memberTypeRef) {
        return nullptr;
    }

    builder.AddMemberAtOffset(Type::PointerType(dieReader_.ReadAddressSize(), memberTypeRef), "ptr", 0);
    return Type::StructureType(builder.Finalize());
}


/// Generic type builder

BinaryNinja::Ref<BinaryNinja::Type> GenericTypeBuilder::Build() {
    auto tag = resolvedDie_.GetTag();
    VerifyDumpDie(IsTypeTag(tag), resolvedDie_);

    if (tag == DW::DW_TAG_base_type) {
        return BaseTypeBuilder{ctx_, resolvedDie_}.Build();
    }

    if (TypeModifierBuilder::IsTypeModifierTag(tag)) {
        return TypeModifierBuilder{ctx_, resolvedDie_}.Build();
    }

    if (tag == DW::DW_TAG_unspecified_type) {
        return NamedTypeReferenceBuilder{ctx_, resolvedDie_}.Build();
    }

    if (!ctx_.TagDieAsProcessing(resolvedDie_)) {
        return NamedTypeReferenceBuilder{ctx_, resolvedDie_}.Build();
    }

    bool isAnonymous = resolvedDieReader_.AttrReader().ReadName("", true).empty();
    if (!isAnonymous && !decodeNamedTypes_) {
        ctx_.UntagDieAsProcessing(resolvedDie_);
        return NamedTypeReferenceBuilder{ctx_, resolvedDie_}.Build();
    }

    auto type = DoBuild();
    if (!type) {
        BinaryNinja::NamedTypeReference ref{
            BNNamedTypeReferenceClass::TypedefNamedTypeClass,
            "", QualifiedName{"__dwarf_bad_type"}};
        type = Type::NamedType(&ref);
    }

    ctx_.UntagDieAsProcessing(resolvedDie_);
    return type;
}

BinaryNinja::Ref<BinaryNinja::Type> GenericTypeBuilder::DoBuild() {
    auto tag = resolvedDie_.GetTag();
    VerifyDumpDie(IsTypeTag(tag), resolvedDie_);

    if (tag == DW::DW_TAG_typedef) {
        return TypedefBuilder{ctx_, resolvedDie_}.Build();
    }

    if (tag == DW::DW_TAG_array_type) {
        return ArrayTypeBuilder{ctx_, resolvedDie_}.Build();
    }

    if (tag == DW::DW_TAG_enumeration_type) {
        return EnumTypeBuilder{ctx_, resolvedDie_}.Build();
    }

    if (tag == DW::DW_TAG_subroutine_type) {
        return FunctionTypeBuilder{ctx_, resolvedDie_}.Build();
    }

    if (CompositeTypeBuilder::IsCompositeTypeTag(tag)) {
        return CompositeTypeBuilder{ctx_, resolvedDie_}.Build();
    }

    if (tag == DW::DW_TAG_ptr_to_member_type) {
        return PointerToMemberTypeBuilder{ctx_, resolvedDie_}.Build();
    }

    BDLogWarn("encountered type die with unknown tag, DIE: {}", resolvedDieReader_.Dump());
    return nullptr;
}


/// Abstract type builder context

bool TypeBuilderContext::TagDieAsProcessing(DwarfDieWrapper &die) {
    auto [_, inserted] = workingSet_.insert(die.GetOffset());
    return inserted;
}

void TypeBuilderContext::UntagDieAsProcessing(DwarfDieWrapper &die) {
    bool ok = workingSet_.erase(die.GetOffset());
    Verify(ok, FatalError);
}

std::optional<uint64_t> TypeBuilderContext::SlideAddress(DwarfOffset offset, uint64_t address) {
    return dwarfContext_.GetSlidAddress(offset, address);
}


/// Named type reference builder

BinaryNinja::Ref<BinaryNinja::Type> NamedTypeReferenceBuilder::Build() {
    BN::QualifiedName qualifiedName{ctx_.DecodeQualifiedName(die_)};
    BN::NamedTypeReference reference{DecodeTypeClass(), "", qualifiedName};
    auto size = TypeSizeDecoder{die_}.Decode();
    return Type::NamedType(&reference, size ? *size : 0);
}

BNNamedTypeReferenceClass NamedTypeReferenceBuilder::DecodeTypeClass() {
    auto tag = die_.GetTag();
    switch (tag) {
        case DW::DW_TAG_typedef:
            return BNNamedTypeReferenceClass::TypedefNamedTypeClass;
        case DW::DW_TAG_enumeration_type:
            return BNNamedTypeReferenceClass::EnumNamedTypeClass;
        case DW::DW_TAG_structure_type:
        case DW::DW_TAG_class_type:
            return BNNamedTypeReferenceClass::StructNamedTypeClass;
        case DW::DW_TAG_union_type:
            return BNNamedTypeReferenceClass::UnionNamedTypeClass;
        default:
            BDLogWarn("encountered die with unexpected tag, DIE: {}", dieReader_.Dump());
            return BNNamedTypeReferenceClass::UnknownNamedTypeClass;
    }
}

/// Type size decoder

std::optional<DwarfDieWrapper> TypeSizeDecoder::ResolveType(DwarfDieWrapper &die) {
    if (!die.IsValid()) {
        return std::nullopt;
    }

    switch (die.GetTag()) {
        case DW::DW_TAG_enumeration_type:
        case DW::DW_TAG_typedef:
        case DW::DW_TAG_const_type:
        case DW::DW_TAG_volatile_type:
        case DW::DW_TAG_atomic_type:
        case DW::DW_TAG_immutable_type:
        case DW::DW_TAG_packed_type:// TODO: verify all
        case DW::DW_TAG_restrict_type:
        case DW::DW_TAG_shared_type:
        case DW_TAG_APPLE_ptrauth_type: {
            auto base = AttributeReader{die}.ReadReference(DW::DW_AT_type);
            if (!base) {
                return die;
            }
            return ResolveType(*base);
        }
        default:
            return die;
    }
}

std::optional<uint64_t> TypeSizeDecoder::Decode() {
    auto die = ResolveType(die_);
    if (!die) {
        return std::nullopt;
    }

    switch (die->GetTag()) {
        case DW::DW_TAG_subroutine_type:
        case DW::DW_TAG_pointer_type:
        case DW::DW_TAG_reference_type:
        case DW::DW_TAG_rvalue_reference_type:
            return die->GetDwarfUnit().GetAddressByteSize();
        case DW::DW_TAG_structure_type:
        case DW::DW_TAG_union_type:
        case DW::DW_TAG_class_type:
        case DW::DW_TAG_base_type:
            return AttributeReader{*die}.ReadUInt(DW::DW_AT_byte_size, true);
        case DW::DW_TAG_array_type:

        default:
            return std::nullopt;
    }
}
