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

#include "debug.h"
#include "name_index.h"
#include "types.h"

using namespace Binja;
using namespace DebugInfo;

namespace DW = llvm::dwarf;

using BinaryNinja::QualifiedName;
using llvm::DWARFDie;

using Type = BinaryNinja::Type;
using TypeRef = BinaryNinja::Ref<Type>;


/// Type builder context

namespace {

class BasicTypeBuilderContext : public TypeBuilderContext {
public:
    BasicTypeBuilderContext(DwarfContextWrapper &dwarfContext)
        : TypeBuilderContext{dwarfContext} {}

    QualifiedName DecodeQualifiedName(DwarfDieWrapper &die) {
        return QualifiedName{DieReader{die}.ReadQualifiedName()};
    }

    DwarfDieWrapper ResolveDie(DwarfDieWrapper &die) {
        return die;
    }
};

}// namespace


/// Name index

void NameIndex::IndexDie(DwarfDieWrapper &die) {
    auto tag = die.GetTag();
    Verify(TypeBuilder::IsTypeTag(tag), FatalError);

    AttributeReader attributeReader{die};
    Verify(!attributeReader.ReadName("", true).empty(), FatalError);

    std::vector<DwarfOffset> hierarchy = DecodeHierarchy(die.GetOffset());
    InsertHierarchy(hierarchy);
}

void NameIndex::InsertHierarchy(const std::vector<DwarfOffset> &hierarchy) {
    Node *node = &root_;
    for (DwarfOffset newDieOffset: hierarchy) {
        DwarfDieWrapper newDie = ResolveDieOffset(newDieOffset);

        std::string name = AttributeReader{newDie}.ReadName("", true);
        if (name.empty()) {
            name = GetAnonymousName(newDie);
        }

        auto it = node->children.find(name);
        if (it != node->children.end()) {
            if (nodeInfoVector_[it->second.info].baseDie != newDie.GetOffset()) {
                node = MergeNode(*node, name, newDieOffset);
            } else {
                node = &it->second;
            }
        } else {
            node = InsertNode(*node, name, newDie.GetOffset());
        }
    }
}

std::vector<DwarfOffset> NameIndex::DecodeHierarchy(DwarfOffset offset) {
    using namespace llvm::dwarf;
    DwarfDieWrapper die = ResolveDieOffset(offset);
    std::vector<DwarfOffset> result;

    std::function<void(DwarfDieWrapper &)> scanContainer = [&](DwarfDieWrapper &die) {
        auto tag = die.GetTag();
        AttributeReader reader{die};
        std::string name = reader.ReadString(DW_AT_name, "", true);

        switch (tag) {
            case DW_TAG_compile_unit:
                return;
            case DW_TAG_namespace:
            case DW_TAG_lexical_block: {
                result.push_back(die.GetOffset());
                break;
            }
            case DW_TAG_enumeration_type:
            case DW_TAG_base_type:
            case DW_TAG_typedef:
            case DW_TAG_template_alias: {
                VerifyDebugDumpDie(!name.empty(), die);
                result.push_back(die.GetOffset());
                break;
            }
            case DW_TAG_class_type: {
                if (auto base = reader.ReadReference(DW_AT_specification)) {
                    scanContainer(*base);
                    return;
                }
                // fallthrough
            }
            case DW_TAG_structure_type:
            case DW_TAG_union_type: {
                if (!reader.HasAttribute(DW_AT_export_symbols)) {
                    result.push_back(die.GetOffset());
                }
                break;
            }
            case DW_TAG_inlined_subroutine: {
                auto base = reader.ReadReference(DW_AT_abstract_origin);
                VerifyDumpDie(base, die);
                scanContainer(*base);
                break;
            }
            case DW_TAG_subprogram: {
                if (auto base = reader.ReadReference(DW_AT_specification)) {
                    scanContainer(*base);
                    return;
                }
                if (auto base = reader.ReadReference(DW_AT_abstract_origin)) {
                    scanContainer(*base);
                    return;
                }
                result.push_back(die.GetOffset());
                break;
            }
            default: {
                throw DwarfError{"unexpected container type {}, DIE: {}",
                                 TagString(tag), DieReader{die}.Dump()};
            }
        }

        DwarfDieWrapper parent = die.GetParent();
        scanContainer(parent);
    };

    switch (die.GetTag()) {
        case DW_TAG_unspecified_type:
        case DW_TAG_variable:
        case DW_TAG_array_type:
        case DW_TAG_base_type:
        case DW_TAG_subroutine_type: {
            result.push_back(offset);
            break;
        }
        default: {
            scanContainer(die);
            break;
        }
    }

    std::reverse(result.begin(), result.end());
    BDVerify(!result.empty());
    return result;
}

NameIndex::Node *NameIndex::MergeNode(Node &parentNode, std::string name, DwarfOffset newDieOffset) {
    Node &baseNode = parentNode.children.find(name)->second;
    NodeInfo &baseNodeInfo = nodeInfoVector_[baseNode.info];
    for (int i = 0; i <= baseNodeInfo.forkIndex; ++i) {
        std::string childName;
        if (i != 0) {
            childName = fmt::format("{}__{}", name, i);
        } else {
            childName = name;
        }

        Node &child = parentNode.children.find(childName)->second;
        NodeInfo &childInfo = nodeInfoVector_[child.info];

        if (childInfo.baseDie == newDieOffset) {
            return &child;
        }

        switch (EvaluateMergeStrategy(childInfo.baseDie, newDieOffset)) {
            case NodeMergeStrategy::replace: {
                aliasMap_.insert({childInfo.baseDie, child.info});
                childInfo.baseDie = newDieOffset;
                return &child;
            }
            case NodeMergeStrategy::alias: {
                aliasMap_.insert({newDieOffset, child.info});
                return &child;
            }
            case NodeMergeStrategy::fork: {
                break;
            }
        }
    }

    std::string newName = fmt::format("{}__{}", name, ++baseNodeInfo.forkIndex);
    return InsertNode(parentNode, newName, newDieOffset);
}

DwarfDieWrapper NameIndex::ResolveDieOffset(DwarfOffset offset) {
    auto it = aliasMap_.find(offset);
    if (it != aliasMap_.end()) {
        return dwarfContext_.GetDIEForOffset(nodeInfoVector_[it->second].baseDie);
    }
    return dwarfContext_.GetDIEForOffset(offset);
}

namespace {

bool IsSameType(const Type &lhs, const Type &rhs);

bool IsSameStructureType(const BinaryNinja::Structure &lhs, const BinaryNinja::Structure &rhs) {
    if (lhs.GetWidth() != rhs.GetWidth()) {
        return false;
    }

    auto lhsMembers = lhs.GetMembers();
    auto rhsMembers = rhs.GetMembers();
    if (lhsMembers.size() != rhsMembers.size()) {
        return false;
    }

    for (size_t i = 0; i < lhsMembers.size(); ++i) {
        const auto &m1 = lhsMembers[i];
        const auto &m2 = rhsMembers[i];
        if (m1.name != m2.name || m1.offset != m2.offset || m1.access != m2.access || m1.scope != m2.scope) {
            return false;
        }
        if (m1.type && m2.type) {
            if (!IsSameType(*m1.type, *m2.type)) {
                return false;
            }
        } else if (m1.type || m2.type) {
            return false;
        }
    }

    return true;
}

bool IsSameType(const Type &lhs, const Type &rhs) {
    if (lhs.IsStructure() && rhs.IsStructure()) {
        auto s1 = lhs.GetStructure();
        auto s2 = rhs.GetStructure();
        return IsSameStructureType(*s1, *s2);
    }
    if (lhs.IsPointer() && rhs.IsPointer()) {
        auto b1 = lhs.GetChildType();
        auto b2 = rhs.GetChildType();
        return IsSameType(*b1, *b2);
    }
    if (lhs.IsNamedTypeRefer() && rhs.IsNamedTypeRefer()) {
        auto n1 = lhs.GetNamedTypeReference();
        auto n2 = rhs.GetNamedTypeReference();
        return n1->GetName() == n2->GetName();
    }
    return BNTypesEqual(lhs.GetObject(), rhs.GetObject());
}

}// namespace

NameIndex::NodeMergeStrategy NameIndex::EvaluateMergeStrategy(DwarfOffset currentDieOffset, DwarfOffset newDieOffset) {
    using namespace DW;
    Verify(currentDieOffset != newDieOffset, FatalError);

    DwarfDieWrapper currentDie = ResolveDieOffset(currentDieOffset);
    auto currentTag = currentDie.GetTag();
    bool currentIsType = TypeBuilder::IsTypeTag(currentTag);

    DwarfDieWrapper newDie = ResolveDieOffset(newDieOffset);
    auto newTag = newDie.GetTag();
    bool newIsType = TypeBuilder::IsTypeTag(newTag);

    if (currentIsType != newIsType) {
        return NameIndex::NodeMergeStrategy::fork;
    }

    if (currentIsType) {
        DwarfDieWrapper resolvedCurrentDie = currentDie;
        if (currentTag == DW_TAG_typedef) {
            if (auto resolved = TypedefBuilder::Resolve(currentDie)) {
                resolvedCurrentDie = ResolveDieOffset(resolved->GetOffset());
            }
        }
        DwarfDieWrapper resolvedNewDie = newDie;
        if (currentTag == DW_TAG_typedef) {
            if (auto resolved = TypedefBuilder::Resolve(newDie)) {
                resolvedNewDie = ResolveDieOffset(resolved->GetOffset());
            }
        }

        if (resolvedNewDie.GetOffset() == resolvedCurrentDie.GetOffset()) {
            return NodeMergeStrategy::alias;
        }

        BasicTypeBuilderContext ctx{dwarfContext_};
        TypeRef currentType = GenericTypeBuilder{ctx, resolvedCurrentDie, true}.Build();
        TypeRef newType = GenericTypeBuilder{ctx, resolvedNewDie, true}.Build();
        if (currentType && newType) {
            if (IsSameType(*currentType, *newType)) {
                return NodeMergeStrategy::alias;
            }
        }
    }

    if ((CompositeTypeBuilder::IsCompositeTypeTag(currentTag) || currentTag == DW_TAG_enumeration_type) && (CompositeTypeBuilder::IsCompositeTypeTag(newTag) || newTag == DW_TAG_enumeration_type)) {
        auto isForwardDeclaration = [](DwarfDieWrapper &die) {
            AttributeReader attributeReader{die};
            return attributeReader.HasAttribute(DW_AT_declaration);
        };
        bool currentIsDecl = isForwardDeclaration(currentDie);
        bool newIsDecl = isForwardDeclaration(newDie);
        if (currentIsDecl && !newIsDecl) {
            return NodeMergeStrategy::replace;
        }
        if (newIsDecl) {
            return NodeMergeStrategy::alias;
        }
    }

    return NodeMergeStrategy::fork;
}

const char *NameIndex::GetAnonymousNameSuffix(DW::Tag tag) {
    using namespace DW;
    switch (tag) {
        case DW_TAG_namespace:
            return "ns";
        case DW_TAG_structure_type:
            return "struct";
        case DW_TAG_class_type:
            return "class";
        case DW_TAG_union_type:
            return "union";
        case DW_TAG_subprogram:
        case DW_TAG_inlined_subroutine:
            return "function";
        case DW_TAG_subroutine_type:
            return "functor";
        case DW_TAG_enumeration_type:
            return "enum";
        case DW_TAG_lexical_block:
            return "block";
        case DW_TAG_unspecified_type:
            return "unknown";
        default:
            throw FatalError{"unexpected dwarf tag {}", TagString(tag)};
    }
}

std::string NameIndex::GetAnonymousName(DwarfDieWrapper &die) {
    return fmt::format("__anon_{}_{:#04x}_{:#08x}", GetAnonymousNameSuffix(die.GetTag()), die.GetOffset().binaryId, die.GetOffset().offset);
}

QualifiedName NameIndex::DecodeQualifiedName(DwarfDieWrapper &die) {
    std::vector<DwarfOffset> hierarchy = DecodeHierarchy(die.GetOffset());
    BDVerify(hierarchy.size() > 0);
    QualifiedName qualifiedName;
    const Node *node = &root_;
    for (DwarfOffset offset: hierarchy) {
        node = node ? FindChild(*node, offset) : nullptr;
        if (node) {
            const auto &info = nodeInfoVector_[node->info];
            qualifiedName.push_back(info.name);
        } else {
            auto child = ResolveDieOffset(offset);
            auto name = AttributeReader{child}.ReadName("", true);
            if (name.empty()) {
                name = GetAnonymousName(child);
            }
            qualifiedName.push_back(name);
        }
    }
    return qualifiedName;
}

const NameIndex::Node *NameIndex::FindChild(const NameIndex::Node &parent, DwarfOffset dieOffset) {
    auto die = ResolveDieOffset(dieOffset);
    std::string name = AttributeReader{die}.ReadName("", true);
    if (name.empty()) {
        name = GetAnonymousName(die);
    }

    auto it = parent.children.find(name);
    if (it == parent.children.end()) {
        return nullptr;
    }

    if (nodeInfoVector_[it->second.info].baseDie == die.GetOffset()) {
        return &it->second;
    }

    for (int i = 0; i <= nodeInfoVector_[it->second.info].forkIndex; i++) {
        std::string forkName = i == 0 ? name : fmt::format("{}__{}", name, i);
        auto forkIt = parent.children.find(forkName);
        const auto &info = nodeInfoVector_[forkIt->second.info];
        if (info.baseDie == dieOffset) {
            return &forkIt->second;
        }
    }

    return nullptr;
}

NameIndex::Node *NameIndex::InsertNode(NameIndex::Node &parent, std::string name, DwarfOffset dieOffset) {
    NodeInfoVectorIndex newEntryInfoIndex = nodeInfoVector_.size();
    NodeInfo newEntryInfo{name, dieOffset, 0};
    nodeInfoVector_.push_back(newEntryInfo);
    auto [it, ok] = parent.children.insert({std::move(name), Node{newEntryInfoIndex, {}}});
    Verify(ok, FatalError);
    ++nodeCount_;
    return &it->second;
}

void NameIndex::VisitEntries(std::function<void(const std::vector<std::string> &, DwarfOffset)> cb) {
    std::vector<std::string> name;
    std::function<void(const Node &)> iterateNode = [&](const Node &node) {
        for (const auto &child: node.children) {
            name.push_back(child.first);
            cb(name, nodeInfoVector_[child.second.info].baseDie);
            iterateNode(child.second);
            name.pop_back();
        }
    };
    iterateNode(root_);
}
