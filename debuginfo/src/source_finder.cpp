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


#include <arpa/inet.h>
#include <fstream>

#include <mio/mmap.hpp>
#include <llvm/Object/MachO.h>

#include <binja/macho/macho.h>
#include <binja/utils/debug.h>
#include <binja/utils/log.h>
#include <binja/utils/span_reader.h>

#include "dsym.h"
#include "source_finder.h"

using namespace Binja;
using namespace DebugInfo;

namespace fs = std::filesystem;

/// KDK

namespace {

static const std::set<uint32_t> kMachOMagics{
    llvm::MachO::MH_MAGIC_64,
    llvm::MachO::MH_CIGAM_64,
};

static const std::set<uint32_t> kFileTypes{
    llvm::MachO::MH_KEXT_BUNDLE,
    llvm::MachO::MH_EXECUTE,

};

static const std::set<uint32_t> kFATMagics{
    llvm::MachO::FAT_MAGIC_64,
    llvm::MachO::FAT_CIGAM_64,
    llvm::MachO::FAT_CIGAM,
    llvm::MachO::FAT_MAGIC,
};

static const std::set<uint32_t> kCPUTypes{
    llvm::MachO::CPU_TYPE_ARM64};

static bool IsSupportedMachO(std::span<const char> data) {
    const auto *header = Utils::SpanReader{data}.Read<llvm::MachO::mach_header_64>();

    if (!kMachOMagics.contains(header->magic)) {
        return false;
    }

    if (!kFileTypes.contains(header->filetype)) {
        return false;
    }

    if (!kCPUTypes.contains(header->cputype)) {
        return false;
    }

    if ((header->flags & llvm::MachO::MH_INCRLINK) != 0) {
        return false;
    }

    return true;
}

static bool IsSupportedFat(std::span<const char> data) {
    Utils::SpanReader reader{data};
    const auto *header = reader.Read<llvm::MachO::fat_header>();

    if (!kFATMagics.contains(header->magic)) {
        return false;
    }

    std::vector<llvm::MachO::fat_arch> supportedArchs;
    for (uint32_t i = 0; i < ntohl(header->nfat_arch); ++i) {
        const auto *arch = reader.Read<llvm::MachO::fat_arch>();
        if (!kCPUTypes.contains(ntohl(arch->cputype))) {
            continue;
        }
        auto size = ntohl(arch->size);
        auto offset = ntohl(arch->offset);
        if (offset + size > data.size()) {
            continue;
        }
        if (IsSupportedMachO(data.subspan(offset, size))) {
            return true;
        }
    }

    return false;
}

}// namespace

void SymbolSourceFinder::VerifyKDK() {
    if (!fs::exists(path_)) {
        throw KDKError{"path {} does not exist", path_.string()};
    }
    if (!fs::is_directory(path_)) {
        throw KDKError{"KDK at path {} is not a directory", path_.string()};
    }
}

std::set<fs::path> SymbolSourceFinder::FindAllDSYMObjects() {
    std::set<fs::path> result;
    if (fs::is_directory(path_) && path_.extension() == ".dSYM") {
        result.insert(path_);
    } else {
        for (const auto &dirent: fs::recursive_directory_iterator(path_)) {
            if (dirent.is_directory() && dirent.path().extension() == ".dSYM") {
                auto [_, ok] = result.insert(dirent.path());
                BDVerify(ok);
            }
        }
    }
    BDLogDebug("found {} dSYM files {}", result.size(), path_.string());
    return result;
}

std::set<fs::path> SymbolSourceFinder::FindAllMachoObjects() {
    std::set<fs::path> result;
    for (const auto &dirent: fs::recursive_directory_iterator(path_)) {
        if (dirent.is_directory()) {
            continue;
        }

        mio::mmap_source file{dirent.path().string()};
        std::span<const char> data{file.data(), file.size()};
        Utils::SpanReader reader{data};

        auto magic = *reader.Read<uint32_t>();

        bool isFat = kFATMagics.contains(magic);
        bool isMacho = kMachOMagics.contains(magic);
        if (!isFat && !isMacho) {
            continue;
        }

        try {
            if (isFat && !IsSupportedFat(data)) {
                continue;
            }
            if (isMacho && !IsSupportedMachO(data)) {
                continue;
            }
        } catch (const Utils::SpanReader::ReadError &e) {
            BDLogWarn("failed to verify file at path {}, error: {}", dirent.path().string(), e.what());
            continue;
        }

        auto [_, ok] = result.insert(dirent.path());
        BDVerify(ok);
    }
    return result;
}

std::set<fs::path> SymbolSourceFinder::FindAllKernelExtensions() {
    std::set<fs::path> result;
    for (const auto &dirent: fs::recursive_directory_iterator(path_)) {
        if (dirent.is_directory() && dirent.path().extension() == "kext") {
            auto [_, ok] = result.insert(dirent.path());
            BDVerify(ok);
        }
    }
    return result;
}
