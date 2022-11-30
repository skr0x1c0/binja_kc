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

#include <limits>
#include <unordered_map>

#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFDie.h>

#include "dwarf.h"
#include "types.h"

namespace Binja::DebugInfo {

class NameIndex {
private:
    struct Node;

    using QualfiedName = BinaryNinja::QualifiedName;
    using NodeInfoVectorIndex = size_t;
    using NodeEntryMap = std::map<std::string, Node>;

    struct Node {
        NodeInfoVectorIndex info;
        NodeEntryMap children;
    };

    struct NodeInfo {
        std::string name;
        DwarfOffset baseDie;
        int forkIndex = 0;
    };

    enum class NodeMergeStrategy {
        fork,
        replace,
        alias
    };

    using NodeInfoVector = std::vector<NodeInfo>;
    using AliasMap = std::unordered_map<DwarfOffset, NodeInfoVectorIndex>;

private:
    const NodeInfoVectorIndex kRootNodeIndex_ = std::numeric_limits<NodeInfoVectorIndex>::max();

public:
    NameIndex(DwarfContextWrapper &dwarfContext) : dwarfContext_{dwarfContext} {}
    void IndexDie(DwarfDieWrapper &die);
    QualfiedName DecodeQualifiedName(DwarfDieWrapper &die);
    DwarfDieWrapper ResolveDieOffset(DwarfOffset offset);
    void VisitEntries(std::function<void(const std::vector<std::string> &, DwarfOffset)> cb);
    size_t NumEntries() const { return nodeCount_; }
    std::vector<DwarfOffset> DecodeHierarchy(DwarfOffset offset);

private:
    void InsertHierarchy(const std::vector<DwarfOffset> &hierarchy);
    NodeMergeStrategy EvaluateMergeStrategy(DwarfOffset currentDieOffset, DwarfOffset newDieOffset);
    NameIndex::Node *MergeNode(Node &parentNode, std::string name, DwarfOffset newDieOffset);
    NameIndex::Node *InsertNode(Node &parent, std::string name, DwarfOffset dieOffset);
    const NameIndex::Node *FindChild(const Node &parent, DwarfOffset dieOffset);

    static const char *GetAnonymousNameSuffix(llvm::dwarf::Tag tag);
    static std::string GetAnonymousName(DwarfDieWrapper &die);

private:
    DwarfContextWrapper &dwarfContext_;
    Node root_{kRootNodeIndex_};
    NodeInfoVector nodeInfoVector_;
    AliasMap aliasMap_;
    size_t nodeCount_ = 0;
};

}// namespace Binja::DebugInfo