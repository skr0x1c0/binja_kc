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
//


#include <binaryninjaapi.h>
#include <binaryninjacore.h>

#include <llvm/Object/MachO.h>
#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

#include <binja/macho/macho.h>
#include <binja/utils/binary_view.h>
#include <binja/utils/debug.h>
#include <binja/utils/log.h>
#include <binja/utils/settings.h>

#include "errors.h"
#include "lib.h"
#include "range.h"

using namespace Binja;
using namespace llvm::MachO;

using Binja::Utils::BinaryViewDataReader;
using KCView::MachoDecodeError;
using KCView::RangeMap;
using MachO::Fileset;
using MachO::MachHeaderParser;
using MachO::MachBinaryViewDataBackend;
using MachO::Section;
using MachO::Segment;

using BinaryNinja::Architecture;
using BinaryNinja::BinaryView;
using BinaryNinja::BinaryViewType;
using BinaryNinja::FileAccessor;
using BinaryNinja::NamedTypeReference;
using BinaryNinja::Platform;
using BinaryNinja::QualifiedName;
using BinaryNinja::Ref;
using BinaryNinja::Settings;
using BinaryNinja::Symbol;
using BinaryNinja::Type;

using Utils::Interval;
using Utils::IntervalMap;

namespace {

constexpr auto *kBinaryType = "MachO-KC";

class CustomBinaryView : public BinaryView {
public:
    explicit CustomBinaryView(BinaryView *parent)
        : BinaryView{kBinaryType, parent->GetFile(), parent} {
        base_ = GetParentView();
        auto bnSettings = BinaryNinja::Settings::Instance();
        Utils::BinjaSettings settings {GetObject(), bnSettings->GetObject()};
        for (const auto& fileset: settings.KCExcludedFilesets()) {
            excludedFilesets_.insert(fileset);
        }
        for (const auto& fileset: settings.KCIncludedFilesets()) {
            includedFilesets_.insert(fileset);
        }
        applyDyldChainedFixups_ = settings.KCApplyDyldChainedFixups();
        stripPAC_ = settings.KCStripPAC();
        defineKallocTypeSymbols_ = settings.KCSymbolicateKallocTypes();
    }

    bool Init() override {
        ProcessKC();
        return true;
    }

    void Parse() {}

    size_t PerformRead(void *dest, uint64_t offset, size_t len) override {
        const auto *segment = va2RawMap_.Query(offset);
        if (!segment) {
            return 0;
        }
        return base_->Read(dest, offset - segment->vaStart + segment->dataStart, len);
    }

    size_t PerformWrite(uint64_t offset, const void *data, size_t len) override {
        BDLogError("PerformWrite not supported");
        return 0;
    }

    size_t PerformInsert(uint64_t offset, const void *data, size_t len) override {
        BDLogError("PerformInsert not supported");
        return 0;
    }

    size_t PerformRemove(uint64_t offset, uint64_t len) override {
        BDLogError("PerformRemove not supported");
        return 0;
    }

    BNModificationStatus PerformGetModification(uint64_t offset) override {
        return BNModificationStatus::Original;
    }

    bool PerformIsValidOffset(uint64_t offset) override {
        return va2RawMap_.Query(offset) != nullptr;
    }

    bool PerformIsOffsetReadable(uint64_t offset) override {
        if (const auto *segment = va2RawMap_.Query(offset)) {
            return segment->flags & BNSegmentFlag::SegmentReadable;
        }
        return false;

    }

    bool PerformIsOffsetWritable(uint64_t offset) override {
        if (const auto *segment = va2RawMap_.Query(offset)) {
            return segment->flags & BNSegmentFlag::SegmentWritable;
        }
        return false;

    }

    bool PerformIsOffsetExecutable(uint64_t offset) override {
        if (const auto *segment = va2RawMap_.Query(offset)) {
            return segment->flags & BNSegmentFlag::SegmentExecutable;
        }
        return false;

    }

    bool PerformIsOffsetBackedByFile(uint64_t offset) override {
        return va2RawMap_.Query(offset + vaStart_) != nullptr;
    }

    uint64_t PerformGetNextValidOffset(uint64_t offset) override {
        return va2RawMap_.FindNextValid(offset);
    }

    uint64_t PerformGetStart() const override {
        return vaStart_;
    }

    uint64_t PerformGetLength() const override {
        return vaLength_;
    }

    uint64_t PerformGetEntryPoint() const override {
        return entryPoint_;
    }

    bool PerformSave(FileAccessor *file) override {
        return base_->Save(file);
    }

    bool PerformIsExecutable() const override { return true; }
    BNEndianness PerformGetDefaultEndianness() const override { return BNEndianness::LittleEndian; }
    bool PerformIsRelocatable() const override { return false; }
    size_t PerformGetAddressSize() const override { return 8; }

private:
    void ProcessKC() {
        VerifyKC();
        FindVAStart();
        ProcessBaseSegments();
        auto filesets = DecodeFilesets();
        for (const auto &fileset: filesets) {
            ProcessFileset(fileset);
        }
        FindVALength();
        FindEntryPoint();

        // TODO: handle case when only file contents are saved??
        if (BNIsBackedByDatabase(GetFile()->GetObject(), kBinaryType)) {
            return;
        }

        if (defineKallocTypeSymbols_) {
            DefineKallocTypeSymbols();
        }

        if (applyDyldChainedFixups_) {
            std::vector<char> buffer{};
            buffer.resize(base_->GetLength());
            size_t read = base_->Read(buffer.data(), 0, buffer.size());
            BDVerify(read == buffer.size());
            ApplyDyldChainedFixups(std::span<char>{buffer.data(), buffer.size()});
            size_t wrote = base_->Write(0, buffer.data(), buffer.size());
            BDVerify(wrote == buffer.size());
        }

        if (stripPAC_) {
            StripPAC();
        }

    }

    void VerifyKC() {
        BinaryViewDataReader reader{base_, 0};
        auto header = reader.Read<mach_header_64>();
        BDVerify(header.magic == MH_MAGIC_64 || header.magic == MH_CIGAM_64);
        BDVerify(header.cputype == CPU_TYPE_ARM64);
        BDVerify(header.cpusubtype == CPU_SUBTYPE_ARM64E);
        SetDefaultArchitecture(Architecture::GetByName("aarch64"));
        SetDefaultPlatform(Platform::GetByName("mac-aarch64"));
    }

    void FindVAStart() {
        auto segments = DecodeSegments(0);
        for (const auto &segment: segments) {
            if (segment.vaStart > 0) {
                vaStart_ = segment.vaStart;
                return;
            }
        }
        throw MachoDecodeError{"Image does not have segment with non zero VA"};
    }

    void FindVALength() {
        uint64_t maxVA = vaStart_;
        for (const auto &segment: va2RawMap_.Values()) {
            maxVA = std::max(segment.vaStart + segment.vaLength, maxVA);
        }
        vaLength_ = maxVA - vaStart_;
    }

    void FindEntryPoint() {
        MachBinaryViewDataBackend backend{*base_};
        if (auto entry = MachHeaderParser{backend, 0}.DecodeEntryPoint()) {
            entryPoint_ = *entry;
        } else {
            throw MachoDecodeError{"binary does not have LC_UNIXTHREAD command"};
        }
    }

    bool ShouldSkipSegment(const Fileset &fileset, const Segment &segment) {
        if (!includedFilesets_.empty() && !includedFilesets_.contains(fileset.name)) {
            return true;
        }
        if (excludedFilesets_.contains(fileset.name)) {
            return true;
        }
        if (segment.name == "__LINKEDIT" || segment.name == "__LINKINFO") {
            return true;
        }
        if (segment.vaLength == 0) {
            return true;
        }
        return false;
    }

    void ProcessBaseSegments() {
        MachBinaryViewDataBackend backend{*base_};
        MachHeaderParser parser{backend, 0};
        std::set<std::string> shouldMap{"__TEXT", "__LINKEDIT"};
        for (const auto &segment: parser.DecodeSegments()) {
            if (!shouldMap.contains(segment.name)) {
                BDLogDebug("skipping base segment {}", segment.name);
                continue;
            }
            if (segment.vaLength == 0) {
                BDLogWarn("base segment {} has no VA", segment.name);
            }
            BDLogDebug("adding base segment {}", segment.name);
            InsertSegment(segment);
        }
    }

    void ProcessFileset(const Fileset &fileset) {
        BDLogInfo("Adding fileset {}", fileset.name.c_str());
        auto segments = DecodeSegments(fileset.fileOffset);
        for (const auto &segment: segments) {
            if (ShouldSkipSegment(fileset, segment)) {
                BDLogDebug("Skipping segment {}", segment.name.c_str());
                continue;
            }
            InsertSegment(segment, fileset.name.c_str());
        }
        AddFilesetDataVariables(fileset);
    }

    void InsertSegment(const Segment &segment, const char *prefix = "") {
        BDVerify(segment.vaStart >= vaStart_);
        auto va = Interval{segment.vaStart, segment.vaStart + segment.vaLength};
        if (const Segment *entry = va2RawMap_.Query(va)) {
            throw MachoDecodeError{"VA overlap between [{:#016x}-{:#016x}) and [{:#016x}-{:#016x}) while trying to add segment {}",
                                   va.lower(), va.upper(),
                                   entry->vaStart, entry->vaStart + entry->vaLength, segment.name};
        }
        va2RawMap_.Insert(va, segment);
        AddAutoSegment(segment.vaStart, segment.vaLength, segment.dataStart, segment.dataLength, segment.flags);
        for (const auto &section: segment.sections) {
            BDLogDebug("Adding section {}", section.name.c_str());
            AddAutoSection(
                fmt::format("{}::{}::{}", prefix, segment.name, section.name),
                section.vaStart,
                section.vaLength,
                section.semantics);
        }
    }

    void ApplyDyldChainedFixups(std::span<char> data) {
        MachO::MachSpanDataBackend backend{data};
        MachO::MachHeaderParser parser {backend, 0};
        std::vector<MachO::DyldChainedPtr> chainedPtrs = parser.DecodeDyldChainedPtrs();
        BDLogInfo("Found {} chained pointers", chainedPtrs.size());
        for (const auto& ptr: chainedPtrs) {
            BDVerify(ptr.fileOffset + sizeof(ptr.value) < data.size_bytes());
            memcpy(&data[ptr.fileOffset], &ptr.value, sizeof(ptr.value));
        }
    }

    void AddFilesetDataVariables(const Fileset &fileset) {
        NamedTypeReference ref{
            BNNamedTypeReferenceClass::StructNamedTypeClass,
            "", QualifiedName{"mach_header_64"}};
        DefineDataVariable(fileset.vmAddr, Type::NamedType(&ref));
        Ref<BinaryNinja::Symbol> symbol = new BinaryNinja::Symbol{
            BNSymbolType::DataSymbol,
            "__mach_header", "__mach_header", "__mach_header", fileset.vmAddr};
        DefineAutoSymbol(symbol);
    }

    void StripPAC() {
        tf::Executor executor;
        tf::Taskflow taskflow;

        std::atomic<size_t> totalXPACs = 0;
        const auto &segments = va2RawMap_.Values();

        taskflow.for_each(segments.begin(), segments.end(), [&](const auto &segment) {
            if (segment.flags & BNSegmentFlag::SegmentExecutable) {
                return;
            }
            if (segment.flags & BNSegmentFlag::SegmentContainsCode) {
                return;
            }

            std::vector<uint64_t> data{};
            data.resize(segment.dataLength / 8);
            size_t dataSize = data.size() * 8;
            auto read = base_->Read(data.data(), segment.dataStart, dataSize);
            if (read < dataSize) {
                data.resize(read / 8);
                dataSize = data.size() * 8;
            }

            size_t numXPAC = 0;
            for (auto it = data.begin(), end = data.end(); it != end; ++it) {
                uint64_t value = *it;
                uint32_t signature = value >> 44;
                if (signature == 0 || signature == 0xfffff) {
                    continue;
                }
                uint8_t checkField = (value >> 40) & 0xf;
                if (checkField != 0xe) {
                    continue;
                }
                uint64_t address = value | 0xfffff00000000000ULL;
                if (address < vaStart_ || address >= vaStart_ + vaLength_ || va2RawMap_.Query(address) == nullptr) {
                    continue;
                }
                *it = address;
                ++numXPAC;
            }

            BDLogInfo("XPACed {} pointer from segment {}", numXPAC, segment.name);
            if (numXPAC) {
                size_t wrote = base_->Write(segment.dataStart, data.data(), dataSize);
                BDVerify(wrote == dataSize);
                totalXPACs += numXPAC;
            }
        });

        executor.run(taskflow).wait();
        BDLogInfo("XPACed total {} pointers", totalXPACs);
    }

    void DefineKallocTypeSymbols() {
        const auto &segments = va2RawMap_.Values();

        NamedTypeReference kallocTypeRef{
            BNNamedTypeReferenceClass::StructNamedTypeClass,
            "",
            QualifiedName{"kalloc_type_view"}};
        Ref<Type> kallocType = Type::NamedType(&kallocTypeRef, 64);

        NamedTypeReference kallocVarRef{
            BNNamedTypeReferenceClass::StructNamedTypeClass,
            "",
            QualifiedName{"kalloc_type_var_view"}};
        Ref<Type> kallocVarType = Type::NamedType(&kallocVarRef, 80);

        BeginBulkModifySymbols();
        size_t totalSymbols = 0;
        for (const auto &segment: segments) {
            for (const auto &section: segment.sections) {
                bool isVar;
                if (section.name.ends_with("__kalloc_var")) {
                    isVar = true;
                } else if (section.name.ends_with("__kalloc_type")) {
                    isVar = false;
                } else {
                    continue;
                }
                size_t size = isVar ? 80 : 64;
                Ref<Type> type = isVar ? kallocVarType : kallocType;
                for (size_t cursor = section.vaStart;
                     (cursor + size) <= section.vaStart + section.vaLength;
                     cursor += size) {
                    Ref<Symbol> symbol = new Symbol{
                        BNSymbolType::DataSymbol,
                        isVar ? "kalloc_type_var_view" : "kalloc_type_view",
                        cursor};
                    DefineAutoSymbolAndVariableOrFunction(GetDefaultPlatform(), symbol, type);
                    ++totalSymbols;
                }
            }
        }
        EndBulkModifySymbols();
        BDLogInfo("defined {} kalloc type (var) view symbols", totalSymbols);
    }

    std::vector<Fileset> DecodeFilesets() {
        MachBinaryViewDataBackend backend{*base_};
        return MachHeaderParser{backend, 0}.DecodeFilesets();
    }

    std::vector<Segment> DecodeSegments(uint64_t fileoff) {
        MachBinaryViewDataBackend backend{*base_};
        return MachHeaderParser{backend, fileoff}.DecodeSegments();
    }

private:
    uint64_t vaStart_;
    uint64_t vaLength_;
    uint64_t entryPoint_;
    RangeMap<uint64_t, Segment> va2RawMap_;
    Ref<BinaryView> base_;

    std::set<std::string> excludedFilesets_;
    std::set<std::string> includedFilesets_;
    bool applyDyldChainedFixups_;
    bool stripPAC_;
    bool defineKallocTypeSymbols_;
};

class CustomBinaryType : public BinaryViewType {
public:
    CustomBinaryType() : BinaryViewType{kBinaryType, "MachO Kernel Cache"} {}

    bool IsDeprecated() override { return false; }

    std::conditional_t<BN_CURRENT_CORE_ABI_VERSION >= 31, Ref<BinaryView>, BinaryView *> Create(BinaryView *data) override {
        try {
            return new CustomBinaryView{data};
        } catch (const Types::DecodeError &e) {
            BDLogError("Failed to decode MachO Kernel Cache, error: {}", e.what());
            return nullptr;
        }
    }

    std::conditional_t<BN_CURRENT_CORE_ABI_VERSION >= 31, Ref<BinaryView>, BinaryView *> Parse(BinaryView *data) override {
        try {
            auto *bv = dynamic_cast<CustomBinaryView *>(data);
            bv->Parse();
            return bv;
        } catch (const Types::DecodeError &e) {
            BDLogError("Failed to parse MachO Kernel Cache, error: {}", e.what());
            return nullptr;
        }
    }

    bool IsTypeValidForData(BinaryView *data) override {
        if (data->GetLength() < sizeof(mach_header_64)) {
            return false;
        }

        BinaryViewDataReader reader{data, 0};
        auto header = reader.Read<mach_header_64>();
        if (header.magic != MH_CIGAM_64 && header.magic != MH_MAGIC_64) {
            return false;
        }
        if (header.filetype != 0xc /* MH_FILESET */) {
            return false;
        }
        BDLogDebug("Matched Kernel Cache");
        return true;
    }

    Ref<Settings> GetLoadSettingsForData(BinaryView *data) override {
        return new Settings{
            BNGetBinaryViewDefaultLoadSettingsForData(m_object, data->GetObject())};
    }
};

}// namespace


void KCView::CorePluginInit() {
    BinaryViewType::Register(new CustomBinaryType{});
}
