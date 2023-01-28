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


#include <llvm/Object/MachO.h>

#include "macho/macho.h"
#include "utils/demangle.h"
#include "utils/log.h"

using namespace Binja;
using namespace MachO;
using namespace llvm::MachO;

namespace BN = BinaryNinja;

using Utils::BinaryViewDataReader;

namespace {

#define LC_FILESET_ENTRY (0x35 | LC_REQ_DYLD)

/*
 * A variable length string in a load command is represented by an lc_str
 * union.  The strings are stored just after the load command structure and
 * the offset is from the start of the load command structure.  The size
 * of the string is reflected in the cmdsize field of the load command.
 * Once again any padded bytes to bring the cmdsize field to a multiple
 * of 4 bytes must be zero.
 */
union lc_str {
    uint32_t offset; /* offset to the string */
#ifndef __LP64__
    char *ptr; /* pointer to the string */
#endif
};

/*
 * LC_FILESET_ENTRY commands describe constituent Mach-O files that are part
 * of a fileset. In one implementation, entries are dylibs with individual
 * mach headers and repositionable text and data segments. Each entry is
 * further described by its own mach header.
 */
struct fileset_entry_command {
    uint32_t cmd;          /* LC_FILESET_ENTRY */
    uint32_t cmdsize;      /* includes entry_id string */
    uint64_t vmaddr;       /* memory address of the entry */
    uint64_t fileoff;      /* file offset of the entry */
    union lc_str entry_id; /* contained entry id */
    uint32_t reserved;     /* reserved */
};

struct arm_unified_thread_state {
    arm_state_hdr_t ash;
    union {
        arm_thread_state32_t ts_32;
        arm_thread_state64_t ts_64;
    } uts;
};

struct dyld_chained_starts_in_segment {
    uint32_t size;             // size of this (amount kernel needs to copy)
    uint16_t page_size;        // 0x1000 or 0x4000
    uint16_t pointer_format;   // DYLD_CHAINED_PTR_*
    uint64_t segment_offset;   // offset in memory to start of segment
    uint32_t max_valid_pointer;// for 32-bit OS, any value beyond this is not a pointer
    uint16_t page_count;       // how many pages are in array
    uint16_t page_start[1];    // each entry is offset in each page of first element in chain
                               // or DYLD_CHAINED_PTR_START_NONE if no fixups on page
                               // uint16_t    chain_starts[1];    // some 32-bit formats may require multiple starts per page.
                               // for those, if high bit is set in page_starts[], then it
                               // is index into chain_starts[] which is a list of starts
                               // the last of which has the high bit set
};

enum {
    DYLD_CHAINED_PTR_START_NONE = 0xFFFF, // used in page_start[] to denote a page with no fixups
    DYLD_CHAINED_PTR_START_MULTI = 0x8000,// used in page_start[] to denote a page which has multiple starts
    DYLD_CHAINED_PTR_START_LAST = 0x8000, // used in chain_starts[] to denote last start in list for page
};

// values for dyld_chained_starts_in_segment.pointer_format
enum {
    DYLD_CHAINED_PTR_ARM64E = 1,// stride 8, unauth target is vmaddr
    DYLD_CHAINED_PTR_64 = 2,    // target is vmaddr
    DYLD_CHAINED_PTR_32 = 3,
    DYLD_CHAINED_PTR_32_CACHE = 4,
    DYLD_CHAINED_PTR_32_FIRMWARE = 5,
    DYLD_CHAINED_PTR_64_OFFSET = 6,    // target is vm offset
    DYLD_CHAINED_PTR_ARM64E_OFFSET = 7,// old name
    DYLD_CHAINED_PTR_ARM64E_KERNEL = 7,// stride 4, unauth target is vm offset
    DYLD_CHAINED_PTR_64_KERNEL_CACHE = 8,
    DYLD_CHAINED_PTR_ARM64E_USERLAND = 9,     // stride 8, unauth target is vm offset
    DYLD_CHAINED_PTR_ARM64E_FIRMWARE = 10,    // stride 4, unauth target is vmaddr
    DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE = 11,// stride 1, x86_64 kernel caches
    DYLD_CHAINED_PTR_ARM64E_USERLAND24 = 12,  // stride 8, unauth target is vm offset, 24-bit bind
};

// DYLD_CHAINED_PTR_ARM64E
struct dyld_chained_ptr_arm64e_rebase {
    uint64_t target : 43,
        high8 : 8,
        next : 11,// 4 or 8-byte stide
        bind : 1, // == 0
        auth : 1; // == 0
};

// DYLD_CHAINED_PTR_ARM64E
struct dyld_chained_ptr_arm64e_bind {
    uint64_t ordinal : 16,
        zero : 16,
        addend : 19,// +/-256K
        next : 11,  // 4 or 8-byte stide
        bind : 1,   // == 1
        auth : 1;   // == 0
};

// DYLD_CHAINED_PTR_ARM64E
struct dyld_chained_ptr_arm64e_auth_rebase {
    uint64_t target : 32,// runtimeOffset
        diversity : 16,
        addrDiv : 1,
        key : 2,
        next : 11,// 4 or 8-byte stide
        bind : 1, // == 0
        auth : 1; // == 1
};

// DYLD_CHAINED_PTR_ARM64E
struct dyld_chained_ptr_arm64e_auth_bind {
    uint64_t ordinal : 16,
        zero : 16,
        diversity : 16,
        addrDiv : 1,
        key : 2,
        next : 11,// 4 or 8-byte stide
        bind : 1, // == 1
        auth : 1; // == 1
};

// DYLD_CHAINED_PTR_64/DYLD_CHAINED_PTR_64_OFFSET
struct dyld_chained_ptr_64_rebase {
    uint64_t target : 36,// 64GB max image size (DYLD_CHAINED_PTR_64 => vmAddr, DYLD_CHAINED_PTR_64_OFFSET => runtimeOffset)
        high8 : 8,       // top 8 bits set to this (DYLD_CHAINED_PTR_64 => after slide added, DYLD_CHAINED_PTR_64_OFFSET => before slide added)
        reserved : 7,    // all zeros
        next : 12,       // 4-byte stride
        bind : 1;        // == 0
};

// DYLD_CHAINED_PTR_64
struct dyld_chained_ptr_64_bind {
    uint64_t ordinal : 24,
        addend : 8,   // 0 thru 255
        reserved : 19,// all zeros
        next : 12,    // 4-byte stride
        bind : 1;     // == 1
};

// DYLD_CHAINED_PTR_64_KERNEL_CACHE, DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE
struct dyld_chained_ptr_64_kernel_cache_rebase {
    uint64_t target : 30,// basePointers[cacheLevel] + target
        cacheLevel : 2,  // what level of cache to bind to (indexes a mach_header array)
        diversity : 16,
        addrDiv : 1,
        key : 2,
        next : 12, // 1 or 4-byte stide
        isAuth : 1;// 0 -> not authenticated.  1 -> authenticated
};

int32_t FixupSegmentMaxProt(const segment_command_64 &cmd) {
    if (std::string(cmd.segname) != "__DATA_CONST") {
        return cmd.maxprot;
    }
    return cmd.maxprot & (~VM_PROT_WRITE);
}

}// namespace

/// Macho header parser

void MachO::MachHeaderParser::VerifyHeader() {
    BinaryViewDataReader reader{&binaryView_, machHeaderOffset_};
    auto cmd = reader.Read<mach_header_64>();
    if (cmd.magic != MH_MAGIC_64 && cmd.magic != MH_CIGAM_64) {
        throw MachHeaderDecodeError{"unsupported mach header magic {} at offset {} for binary {}", cmd.magic, machHeaderOffset_,
                                    binaryView_.GetFile()->GetOriginalFilename()};
    }
}

Fileset MachHeaderParser::DecodeFileset(BinaryViewDataReader &reader) {
    auto cmd = reader.Peek<fileset_entry_command>();
    reader.Seek(cmd.entry_id.offset);
    std::string name = reader.ReadString();
    return Fileset{
        .name = name,
        .vmAddr = cmd.vmaddr,
        .fileOffset = cmd.fileoff,
    };
}

std::vector<Fileset> MachHeaderParser::DecodeFilesets() {
    BinaryViewDataReader reader{&binaryView_, machHeaderOffset_};
    auto header = reader.Read<mach_header_64>();
    std::vector<Fileset> result;
    for (int i = 0; i < header.ncmds; ++i) {
        auto cmd = reader.Peek<load_command>();
        if (cmd.cmd == LC_FILESET_ENTRY) {
            BinaryViewDataReader sub{reader};
            result.push_back(DecodeFileset(sub));
        }
        reader.Seek(cmd.cmdsize);
    }
    return result;
}

std::vector<Section> MachHeaderParser::DecodeSections(BinaryViewDataReader &reader) {
    auto segment = reader.Read<segment_command_64>();

    auto decodeSectionSemantics = [](const segment_command_64 &segment, const section_64 &section) {
        auto maxProt = FixupSegmentMaxProt(segment);
        if (maxProt & VM_PROT_EXECUTE) {
            return BNSectionSemantics::ReadOnlyCodeSectionSemantics;
        }
        if (!(maxProt & VM_PROT_WRITE)) {
            return BNSectionSemantics::ReadOnlyDataSectionSemantics;
        }
        assert(maxProt & VM_PROT_READ);
        return BNSectionSemantics::ReadWriteDataSectionSemantics;
    };

    std::vector<Section> result;
    result.reserve(segment.nsects);
    for (int i = 0; i < segment.nsects; ++i) {
        auto section = reader.Read<section_64>();
        result.push_back(Section{
            .name = section.sectname,
            .vaStart = section.addr,
            .vaLength = section.size,
            .semantics = decodeSectionSemantics(segment, section),
        });
    }
    return result;
}

Segment MachHeaderParser::DecodeSegment(BinaryViewDataReader &reader) {
    auto decodeSegmentFlags = [](const segment_command_64 &cmd) {
        uint32_t flags = 0;
        auto maxProt = FixupSegmentMaxProt(cmd);
        if (maxProt & VM_PROT_EXECUTE) {
            flags |= BNSegmentFlag::SegmentContainsCode;
            flags |= BNSegmentFlag::SegmentExecutable;
            flags |= BNSegmentFlag::SegmentDenyWrite;
        }
        if (maxProt & VM_PROT_READ) {
            flags |= BNSegmentFlag::SegmentReadable;
        }
        if ((maxProt & VM_PROT_WRITE)) {
            flags |= BNSegmentFlag::SegmentWritable;
            flags |= BNSegmentFlag::SegmentDenyExecute;
        }
        return flags;
    };

    auto cmd = reader.Peek<segment_command_64>();
    Segment result{
        .name = cmd.segname,
        .vaStart = cmd.vmaddr,
        .vaLength = cmd.vmsize,
        .dataStart = cmd.fileoff,
        .dataLength = cmd.filesize,
        .flags = decodeSegmentFlags(cmd),
    };
    result.sections = DecodeSections(reader);
    return result;
}

std::vector<Segment> MachHeaderParser::DecodeSegments() {
    BinaryViewDataReader reader{&binaryView_, machHeaderOffset_};
    auto header = reader.Read<mach_header_64>();
    std::vector<Segment> result;
    for (int i = 0; i < header.ncmds; ++i) {
        auto cmd = reader.Peek<load_command>();
        if (cmd.cmd == LC_SEGMENT_64) {
            BinaryViewDataReader sub{reader};
            result.push_back(DecodeSegment(sub));
        }
        reader.Seek(cmd.cmdsize);
    }
    return result;
}

std::optional<uint64_t> MachHeaderParser::DecodeEntryPoint() {
    BinaryViewDataReader reader{&binaryView_, machHeaderOffset_};
    auto header = reader.Read<mach_header_64>();
    for (int i = 0; i < header.ncmds; ++i) {
        auto cmd = reader.Peek<load_command>();
        if (cmd.cmd != LC_UNIXTHREAD) {
            reader.Seek(cmd.cmdsize);
            continue;
        }
        auto sub = BinaryViewDataReader{reader};
        sub.Seek(sizeof(thread_command));
        auto flavor = sub.Peek<uint32_t>();
        if (flavor != ARM_THREAD_STATE64) {
            throw MachHeaderDecodeError{"unsupported LC_UNIXTHREAD flavor {}", flavor};
        }
        auto state = sub.Read<arm_unified_thread_state>();
        return state.uts.ts_64.pc;
    }
    return std::nullopt;
}

std::optional<Types::UUID> MachHeaderParser::DecodeUUID() {
    if (auto uuid = FindCommand<uuid_command>(LC_UUID)) {
        Types::UUID result;
        static_assert(sizeof(result.data) == sizeof(uuid->uuid));
        memcpy(result.data, uuid->uuid, sizeof(result.data));
        return result;
    }
    return std::nullopt;
}


std::vector<Symbol> MachHeaderParser::DecodeSymbols() {
    std::vector<Symbol> result;
    if (auto symtab = FindCommand<symtab_command>(LC_SYMTAB)) {
        BinaryViewDataReader symReader{&binaryView_, symtab->symoff};
        for (size_t i = 0; i < symtab->nsyms; ++i) {
            auto sym = symReader.Read<nlist_64>();
            if ((sym.n_type & N_TYPE) == N_UNDF) {
                continue;
            }
            BinaryViewDataReader strReader{&binaryView_, symtab->stroff + sym.n_strx};
            std::string name = Utils::Demangle(strReader.ReadString());
            result.emplace_back(Symbol{
                .name = name,
                .addr = sym.n_value,
            });
        }
    }
    return result;
}

std::vector<DyldChainedPtr> MachHeaderParser::DecodeDyldChainedPtrs() {
    auto cmd = FindCommand<linkedit_data_command>(LC_DYLD_CHAINED_FIXUPS);
    if (!cmd) {
        BDLogWarn("Skipping DYLD_CHAINED_FIXUPS since no LC_DYLD_CHAINED_FIXUPS command found");
        return std::vector<DyldChainedPtr>();
    }

    uint64_t vmBase = *FindVMBase();
    std::vector<DyldChainedPtr> result;

    BinaryViewDataReader startsInImageReader{&binaryView_, cmd->dataoff};
    auto fixupsHeader = startsInImageReader.Peek<dyld_chained_fixups_header>();
    startsInImageReader.Seek(fixupsHeader.starts_offset);

    auto startsInImageHeader = startsInImageReader.Peek<dyld_chained_starts_in_image>();
    startsInImageReader.Seek(offsetof(dyld_chained_starts_in_image, seg_info_offset));

    for (size_t segInfoIndex = 0; segInfoIndex < startsInImageHeader.seg_count; ++segInfoIndex) {
        static_assert(sizeof(dyld_chained_starts_in_image::seg_info_offset) == sizeof(uint32_t));
        auto segInfoOffset = startsInImageReader.Read<uint32_t>();
        if (segInfoOffset == 0) {
            continue;
        }

        BinaryViewDataReader startsInSegmentReader{&binaryView_, cmd->dataoff + fixupsHeader.starts_offset + segInfoOffset};
        auto startsInSegmentHeader = startsInSegmentReader.Peek<dyld_chained_starts_in_segment>();

        static_assert(offsetof(dyld_chained_starts_in_segment, page_start) + sizeof(dyld_chained_starts_in_segment::page_start) == sizeof(dyld_chained_starts_in_segment));
        static_assert(sizeof(dyld_chained_starts_in_segment::page_start) == sizeof(uint16_t));

        startsInSegmentReader.Seek(offsetof(dyld_chained_starts_in_segment, page_start));
        for (size_t pageIndex = 0; pageIndex < startsInSegmentHeader.page_count; ++pageIndex) {
            auto offsetInPage = startsInSegmentReader.Read<uint16_t>();
            if (offsetInPage == DYLD_CHAINED_PTR_START_NONE) {
                continue;
            }
            if (offsetInPage & DYLD_CHAINED_PTR_START_MULTI) {
                BDLogWarn("Skipping DYLD_CHAINED_PTR_START_MULTI");
                continue;
            }
            switch (startsInSegmentHeader.pointer_format) {
                case DYLD_CHAINED_PTR_64_KERNEL_CACHE: {
                    union PtrARM64e {
                        uint64_t raw;
                        dyld_chained_ptr_arm64e_rebase rebase;
                        dyld_chained_ptr_arm64e_bind bind;
                        dyld_chained_ptr_arm64e_auth_rebase auth_rebase;
                        dyld_chained_ptr_arm64e_auth_bind auth_bind;
                    };
                    BinaryViewDataReader ptrReader{
                        &binaryView_,
                        startsInSegmentHeader.segment_offset + pageIndex * startsInSegmentHeader.page_size + offsetInPage};
                    while (true) {
                        auto ptr = ptrReader.Peek<PtrARM64e>();
                        bool auth = ptr.rebase.auth;
                        bool bind = ptr.rebase.bind;
                        auto next = ptr.rebase.next;

                        if (auth && bind) {
                            BDLogWarn("Cannot fixup chained pointer with both auth and bind set "
                                      "at offset {:#016x}", ptrReader.Offset());
                        } else if (auth) {
                            auto address = vmBase + ptr.auth_rebase.target;
                            result.emplace_back(DyldChainedPtr{
                                .fileOffset = ptrReader.Offset(),
                                .value = address,
                            });
                        } else if (bind) {
                            BDLogWarn("Cannot chained pointer with bind set "
                                      "at offset {:#016x}", ptrReader.Offset());
                        } else {
                            auto top8Bits = (ptr.raw >> 43) & 0xFFL;
                            auto bottom43Bits = ptr.raw & 0x000007FFFFFFFFFFL;
                            if (top8Bits == 0x80) {
                                top8Bits = 0;
                            }
                            auto address = vmBase + ((top8Bits << 56) | bottom43Bits);
                            result.emplace_back(DyldChainedPtr{
                                .fileOffset = ptrReader.Offset(),
                                .value = address,
                            });
                        }

                        if (next == 0) {
                            break;
                        }
                        ptrReader.Seek(next * 4);
                    }
                    break;
                }
                default:
                    BDLogWarn("Encountered unknown pointer format {}, skipping", startsInSegmentHeader.pointer_format);
                    continue;
            }
        }
    }

    return result;
}

template<class T>
std::optional<T> MachHeaderParser::FindCommand(uint32_t cmd) {
    BinaryViewDataReader reader{&binaryView_, machHeaderOffset_};
    auto header = reader.Read<mach_header_64>();
    for (uint32_t i = 0; i < header.ncmds; ++i) {
        auto lc = reader.Peek<load_command>();
        if (lc.cmd != cmd) {
            reader.Seek(lc.cmdsize);
            continue;
        }
        return reader.Peek<T>();
    }
    return std::nullopt;
}

std::optional<uint64_t> MachHeaderParser::FindVMBase() {
    BinaryViewDataReader reader{&binaryView_, machHeaderOffset_};
    auto header = reader.Read<mach_header_64>();
    for (uint32_t i = 0; i < header.ncmds; ++i) {
        auto lc = reader.Peek<load_command>();
        if (lc.cmd != LC_SEGMENT_64) {
            reader.Seek(lc.cmdsize);
            continue;
        }
        auto segment = reader.Peek<segment_command_64>();
        if (segment.vmaddr > 0) {
            return segment.vmaddr;
        }
        reader.Seek(lc.cmdsize);
    }
    return std::nullopt;
}

/// Mach binary view

std::vector<uint64_t> MachBinaryView::ReadMachOHeaderOffsets() {
    uint64_t start = binaryView_.GetStart();
    std::vector<uint64_t> result{start};
    MachHeaderParser header{binaryView_, start};
    for (const auto &fileset: header.DecodeFilesets()) {
        if (binaryView_.GetTypeName() == "Raw") {
            result.push_back(fileset.fileOffset + binaryView_.GetStart());
        } else {
            result.push_back(fileset.vmAddr);
        }
    }
    return result;
}

std::map<Types::UUID, std::vector<Segment>> MachBinaryView::ReadMachOHeaders() {
    std::map<Types::UUID, std::vector<Segment>> result;
    for (const auto offset: ReadMachOHeaderOffsets()) {
        if (!binaryView_.IsValidOffset(offset)) {
            continue;
        }
        MachHeaderParser parser{binaryView_, offset};
        auto uuid = parser.DecodeUUID();
        if (!uuid) {
            BDLogWarn("mach header at {:#016x} does not have LC_UUID command, "
                      "symbols won't be loaded this segments in this header",
                      offset);
            continue;
        }
        result[*uuid] = parser.DecodeSegments();
    }
    return result;
}
