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

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <binaryninjaapi.h>

#include "dwarf.h"

namespace Binja::DebugInfo {

class TypeBuilderContext {
public:
    using Type = BinaryNinja::Type;
    using TypeRef = BinaryNinja::Ref<Type>;
    using QualifiedName = BinaryNinja::QualifiedName;

public:
    TypeBuilderContext(DwarfContextWrapper &dwarfContext) : dwarfContext_{dwarfContext} {}
    virtual ~TypeBuilderContext() = default;
    virtual BinaryNinja::QualifiedName DecodeQualifiedName(DwarfDieWrapper &die) = 0;
    virtual DwarfDieWrapper ResolveDie(DwarfDieWrapper &die) = 0;
    virtual bool TagDieAsProcessing(DwarfDieWrapper &die);
    virtual void UntagDieAsProcessing(DwarfDieWrapper &die);
    virtual std::optional<uint64_t> SlideAddress(DwarfOffset die, uint64_t address);

protected:
    DwarfContextWrapper &dwarfContext_;
    std::unordered_set<DwarfOffset> workingSet_;
};

class TypeBuilder {
public:
    TypeBuilder(TypeBuilderContext &ctx, DwarfDieWrapper &die)
        : ctx_{ctx}, die_{die}, dieReader_{die}, attributeReader_{die} {}

    virtual BinaryNinja::Ref<BinaryNinja::Type> Build() = 0;

    static bool IsTypeTag(llvm::dwarf::Tag tag);

protected:
    TypeBuilderContext &ctx_;
    DwarfDieWrapper &die_;
    DieReader dieReader_;
    AttributeReader attributeReader_;
};

class GenericTypeBuilder : public TypeBuilder {
public:
    GenericTypeBuilder(TypeBuilderContext &ctx, DwarfDieWrapper &die, bool decodeNamedTypes = false)
        : TypeBuilder{ctx, die}, decodeNamedTypes_{decodeNamedTypes},
          resolvedDie_{ctx.ResolveDie(die)}, resolvedDieReader_{resolvedDie_} {}
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;

private:
    BinaryNinja::Ref<BinaryNinja::Type> DoBuild();

private:
    bool decodeNamedTypes_;
    DwarfDieWrapper resolvedDie_;
    DieReader resolvedDieReader_;
};

class BaseTypeBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;
    BinaryNinja::Ref<BinaryNinja::Type> MapBNType(uint64_t size, uint64_t encoding);
};

class TypeModifierBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;
    static bool IsTypeModifierTag(llvm::dwarf::Tag tag);
};

class TypedefBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;
    static std::optional<DwarfDieWrapper> Resolve(DwarfDieWrapper &die);
};

class ArrayTypeBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;

private:
    BinaryNinja::Ref<BinaryNinja::Type> BuildDynamic();
    BinaryNinja::Ref<BinaryNinja::Type> BuildStatic();
    std::optional<size_t> DecodeCountFromSubrange(DwarfDieWrapper &die);
    size_t GetDefaultLowerBound();
};

class FunctionTypeBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;

private:
    struct DecodeParametersResult {
        bool hasVarArg;
        std::vector<BinaryNinja::FunctionParameter> parameters;
    };

private:
    BinaryNinja::Ref<BinaryNinja::Type> DecodeReturnType();
    BinaryNinja::Ref<BinaryNinja::CallingConvention> DecodeCallingConvention();
    DecodeParametersResult DecodeParameters();
    BinaryNinja::Ref<BinaryNinja::Type> DecodeParameterType(DwarfDieWrapper &die);
    BinaryNinja::Ref<BinaryNinja::Type> ApplyParameterTypeModifiers(BinaryNinja::Ref<BinaryNinja::Type> type, DwarfDieWrapper &die);
};

class EnumTypeBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;

private:
    std::optional<DwarfDieWrapper> ResolveBaseType();
};

class CompositeTypeBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;
    static bool IsCompositeTypeTag(llvm::dwarf::Tag tag);


private:
    BNStructureVariant DecodeVariant();
    BNMemberAccess GetDefaultMemberAccess();
    bool IsPacked();
    uint8_t DecodeAlignment();
    size_t DecodeWidth();

private:
    struct DecodeMemberResult {
        BinaryNinja::Ref<BinaryNinja::Type> type;
        std::string name;
        uint64_t offset;
        BNMemberAccess access;
    };

    struct DecodeVariableResult {
        BinaryNinja::Ref<BinaryNinja::Type> type;
        std::string name;
        BNMemberAccess access;
    };

private:
    std::optional<DecodeMemberResult> DecodeMember(DwarfDieWrapper &die);
    BNMemberAccess DecodeMemberAccess(std::optional<uint64_t> accessibility);
    std::optional<DecodeVariableResult> DecodeVariable(DwarfDieWrapper &die);
    void ProcessBitfields(BinaryNinja::StructureBuilder &builder);
    std::optional<DwarfDieWrapper> ProcessBitfield(BinaryNinja::StructureBuilder &builder, DwarfDieWrapper &start);
};

class PointerToMemberTypeBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;
};

class NamedTypeReferenceBuilder : public TypeBuilder {
public:
    using TypeBuilder::TypeBuilder;
    BinaryNinja::Ref<BinaryNinja::Type> Build() override;
    BNNamedTypeReferenceClass DecodeTypeClass();
};

class TypeSizeDecoder {
public:
    TypeSizeDecoder(DwarfDieWrapper die) : die_{die} {}
    std::optional<uint64_t> Decode();

private:
    static std::optional<DwarfDieWrapper> ResolveType(DwarfDieWrapper &die);

private:
    DwarfDieWrapper die_;
};

}// namespace Binja::DebugInfo