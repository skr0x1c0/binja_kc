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

#include <functional>
#include <optional>

#include <fmt/format.h>
#include <llvm/DebugInfo/DWARF/DWARFDie.h>

#include <binja/utils/debug.h>

#include "debug.h"
#include "dsym.h"
#include "errors.h"
#include "slider.h"

namespace Binja::DebugInfo {

struct DwarfOffset {
    uint64_t
        binaryId : 16,
        offset : 48;
    auto operator<=>(const DwarfOffset &oth) const = default;
};

class DwarfDieWrapper;
class DwarfDebugInfoEntryWrapper;
class DwarfUnitWrapper;

}// namespace Binja::DebugInfo

namespace Binja::DebugInfo::Detail {

class DwarfDieWrapperIterator;

}


namespace std {

template<>
struct hash<Binja::DebugInfo::DwarfOffset> {
    std::size_t operator()(Binja::DebugInfo::DwarfOffset const &s) const noexcept {
        static_assert(sizeof(s) == sizeof(uint64_t));
        auto v = *reinterpret_cast<const uint64_t *>(&s);
        return hash<uint64_t>{}(v);
    }
};

}// namespace std


namespace Binja::DebugInfo {

using BinaryId = uint16_t;

class DwarfDebugInfoEntryWrapper {
public:
    DwarfDebugInfoEntryWrapper(llvm::DWARFDebugInfoEntry entry, BinaryId binaryId)
        : entry_{entry}, binaryId_{binaryId} {}

    [[nodiscard]] DwarfOffset GetOffset() const { return DwarfOffset{binaryId_, entry_.getOffset()}; }

private:
    llvm::DWARFDebugInfoEntry entry_;
    BinaryId binaryId_;
};

class DwarfUnitWrapper {
public:
    DwarfUnitWrapper(llvm::DWARFUnit &unit, BinaryId binaryId)
        : unit_{unit}, binaryId_{binaryId} {}

    [[nodiscard]] uint8_t GetAddressByteSize() const;
    [[nodiscard]] std::vector<DwarfDebugInfoEntryWrapper> Dies() const;
    [[nodiscard]] const llvm::dwarf::FormParams GetFormParams();

private:
    llvm::DWARFUnit &unit_;
    BinaryId binaryId_;
};

class DwarfDieWrapper {
private:
    using Tag = llvm::dwarf::Tag;
    using Attribute = llvm::dwarf::Attribute;
    using DWARFFormValue = llvm::DWARFFormValue;
    using IteratorTransform = std::function<DwarfDieWrapper(const llvm::DWARFDie &)>;
    using Iterator = llvm::iterator_range<Detail::DwarfDieWrapperIterator>;
    template<class T> using Optional = llvm::Optional<T>;

public:
    DwarfDieWrapper(llvm::DWARFDie die, BinaryId binaryId)
        : die_{die}, offset_{binaryId, 0} {
        if (die.isValid()) {
            offset_.offset = die_.getOffset();
        }
    }

    DwarfDieWrapper()
        : die_{}, offset_{std::numeric_limits<BinaryId>::max()} {}

    [[nodiscard]] Tag GetTag() const { return die_.getTag(); }
    [[nodiscard]] DwarfOffset GetOffset() const { return offset_; }
    [[nodiscard]] Optional<DWARFFormValue> Find(Attribute attr) const { return die_.find(attr); }
    [[nodiscard]] Optional<DWARFFormValue> FindRecursively(Attribute attr) const { return die_.findRecursively(attr); }
    [[nodiscard]] bool IsValid() { return die_.isValid(); }
    [[nodiscard]] bool operator==(const DwarfDieWrapper &oth) const;

    [[nodiscard]] Optional<DwarfDieWrapper> GetAttributeValueAsReferencedDie(const llvm::DWARFFormValue &value) const;
    [[nodiscard]] DwarfUnitWrapper GetDwarfUnit() const;
    [[nodiscard]] DwarfDieWrapper GetParent() const;
    [[nodiscard]] DwarfDieWrapper GetSibling();
    [[nodiscard]] DwarfDieWrapper GetPreviousSibling();
    [[nodiscard]] DwarfDieWrapper GetFirstChild();
    [[nodiscard]] DwarfDieWrapper GetLastChild();
    [[nodiscard]] Iterator Children();
    [[nodiscard]] llvm::Expected<llvm::DWARFAddressRangesVector> GetAddressRanges();
    [[nodiscard]] llvm::Expected<llvm::DWARFLocationExpressionsVector> GetLocations(llvm::dwarf::Attribute attr);
    void Dump(llvm::raw_ostream &ss, uint32_t indent = 0, llvm::DIDumpOptions opts = llvm::DIDumpOptions{});

private:
    llvm::DWARFDie die_;
    DwarfOffset offset_;
};

class DwarfContextWrapper {
public:
    struct Entry {
        DwarfObjectFile object;
        AddressSlider slider;
    };

public:
    explicit DwarfContextWrapper(std::vector<Entry> entries)
        : entries_{std::move(entries)} {
        BDVerify(entries_.size() <= std::numeric_limits<BinaryId>::max());
    }

    [[nodiscard]] DwarfDieWrapper GetDIEForOffset(DwarfOffset offset);
    [[nodiscard]] std::vector<DwarfUnitWrapper> GetNormalUnitsVector();
    [[nodiscard]] std::optional<uint64_t> GetSlidAddress(DwarfOffset offset, uint64_t source);
    [[nodiscard]] size_t GetDwarfObjectCount() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

class AttributeReader {
public:
    explicit AttributeReader(DwarfDieWrapper &die) : die_{die} {}
    [[nodiscard]] std::optional<uint64_t> ReadUInt(llvm::dwarf::Attribute attribute, bool recursive = false) const;
    [[nodiscard]] std::optional<uint64_t> ReadInt(llvm::dwarf::Attribute attribute, bool recursive = false) const;
    [[nodiscard]] std::string ReadString(llvm::dwarf::Attribute attribute, const char *defaultValue = "", bool recursive = false) const;
    [[nodiscard]] std::string ReadName(const char *defaultName = "", bool recursive = false) const;
    [[nodiscard]] std::optional<DwarfDieWrapper> ReadReference(llvm::dwarf::Attribute attribute, bool recursive = false) const;
    [[nodiscard]] bool HasAttribute(llvm::dwarf::Attribute attribute, bool recursive = false) const;
    [[nodiscard]] std::string ReadLinkageName(const char *defaultName = "", bool recursive = false) const;
    [[nodiscard]] std::optional<llvm::DWARFFormValue> FindAttribute(llvm::dwarf::Attribute attr, bool recursive) const;
    [[nodiscard]] std::optional<uint64_t> ReadLocationAddress() const;

private:
    DwarfDieWrapper &die_;
};

class DieReader {
public:
    explicit DieReader(DwarfDieWrapper &die) : die_{die}, attrReader_{die} {}
    const AttributeReader &AttrReader() const { return attrReader_; }
    std::vector<std::string> ReadQualifiedName() const;
    uint8_t ReadAddressSize() const;
    [[nodiscard]] std::string Dump() const;

private:
    DwarfDieWrapper &die_;
    AttributeReader attrReader_;
};

}// namespace Binja::DebugInfo


namespace Binja::DebugInfo::Detail {

class DwarfDieWrapperIterator
    : public llvm::iterator_facade_base<DwarfDieWrapperIterator, std::bidirectional_iterator_tag,
                                        const DwarfDieWrapperIterator> {

    friend std::reverse_iterator<llvm::DWARFDie::iterator>;
    friend bool operator==(const DwarfDieWrapperIterator &lhs,
                           const DwarfDieWrapperIterator &rhs);

public:
    DwarfDieWrapperIterator() = default;

    explicit DwarfDieWrapperIterator(DwarfDieWrapper die) : die_(die) {}

    DwarfDieWrapperIterator &operator++() {
        die_ = die_.GetSibling();
        return *this;
    }

    DwarfDieWrapperIterator &operator--() {
        die_ = die_.GetPreviousSibling();
        return *this;
    }

    const DwarfDieWrapper &operator*() const { return die_; }

private:
    DwarfDieWrapper die_;
};

inline bool operator==(const DwarfDieWrapperIterator &v1,
                       const DwarfDieWrapperIterator &v2) {
    return v1.die_ == v2.die_;
}

}// namespace Binja::DebugInfo::Detail

namespace fmt {

template<>
struct formatter<Binja::DebugInfo::DwarfOffset> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const Binja::DebugInfo::DwarfOffset &p, FormatContext &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "b{}o{}", p.binaryId, p.offset);
    }
};

}// namespace fmt