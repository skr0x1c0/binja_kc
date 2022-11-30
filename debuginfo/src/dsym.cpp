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
#include <llvm/Object/MachO.h>
#include <llvm/Object/MachOUniversal.h>
#include <llvm/Support/Error.h>

#include <binja/types/errors.h>
#include <binja/utils/debug.h>

#include "debug.h"
#include "dsym.h"


using namespace Binja;
using namespace DebugInfo;
using namespace llvm;

namespace fs = std::filesystem;

/// LLVM error utility methods

namespace {

std::string LLVMErrorToString(llvm::Error error) {
    std::string out;
    llvm::raw_string_ostream ss{out};
    logAllUnhandledErrors(std::move(error), ss, "");
    return out;
}

std::string LLVMErrorToString(std::error_code ec) {
    llvm::Error error = llvm::errorCodeToError(ec);
    return LLVMErrorToString(std::move(error));
}

}// namespace


/// Dwarf object file

DwarfObjectFile::DwarfObjectFile(const std::filesystem::path &objectPath) {
    {
        ErrorOr<std::unique_ptr<MemoryBuffer>> buff = MemoryBuffer::getFileOrSTDIN(objectPath.string());
        if (!buff) {
            throw DwarfError{"failed to open file {}, error: {}", objectPath.string(),
                             LLVMErrorToString(buff.getError())};
        }
        buffer_ = std::move(buff.get());
    }

    {
        Expected<std::unique_ptr<object::Binary>> binary = object::createBinary(*buffer_);
        if (!binary) {
            throw DwarfError{"invalid dwarf symbol file {}, error: {}", objectPath.string(),
                             LLVMErrorToString(binary.takeError())};
        }
        binaryObject_ = std::move(binary.get());
    }

    if (auto *obj = llvm::dyn_cast<object::ObjectFile>(&*binaryObject_)) {
        dwarfContext_ = DWARFContext::create(*obj, DWARFContext::ProcessDebugRelocations::Process);
        Verify(dwarfContext_, FatalError);
        return;
    }

    if (auto *fat = dyn_cast<object::MachOUniversalBinary>(&*binaryObject_)) {
        for (auto &obj: fat->objects()) {
            if (auto mach = obj.getAsObjectFile()) {
                std::unique_ptr<object::MachOObjectFile> machObj = std::move(*mach);
                if (machObj->getArch() == llvm::Triple::aarch64) {
                    dwarfContext_ = DWARFContext::create(*machObj, DWARFContext::ProcessDebugRelocations::Process);
                    binaryObject_ = std::move(machObj);
                    Verify(dwarfContext_, FatalError);
                    return;
                }
            } else {
                throw DwarfError{"failed to open universal macho, err: {}", LLVMErrorToString(mach.takeError())};
            }
        }
        throw DwarfError{"dwarf object file does not have symbols for aarch64 architecture"};
    }
    throw DwarfError{"invalid dwarf object file"};
}

std::vector<fs::path> DwarfObjectFile::DsymFindObjects(const fs::path &symbolsPath) {
    std::vector<fs::path> objectPaths;
    if (auto objects = object::MachOObjectFile::findDsymObjectMembers(symbolsPath.string())) {
        if (objects->empty()) {
            objectPaths.push_back(symbolsPath);
        } else {
            for (const auto &object: *objects) {
                auto result = object::createBinary(object);
                if (result) {
                    objectPaths.push_back(object);
                } else if (llvm::errorToErrorCode(result.takeError()) != object::object_error::invalid_file_type) {
                    throw FatalError{"unexpected error: {}", LLVMErrorToString(result.takeError())};
                }
            }
        }
    } else {
        throw DwarfError{"invalid symbols file {}, error: {}", symbolsPath.string(),
                         LLVMErrorToString(objects.takeError())};
    }
    return objectPaths;
}

std::optional<Types::UUID> DwarfObjectFile::DecodeUUID() const {
    auto *macho = llvm::dyn_cast<llvm::object::MachOObjectFile>(dwarfContext_->getDWARFObj().getFile());
    BDVerify(macho);
    for (auto lc: macho->load_commands()) {
        if (lc.C.cmd != llvm::MachO::LC_UUID) {
            continue;
        }
        Types::UUID result;
        if (lc.C.cmdsize < sizeof(lc.C) + sizeof(result.data)) {
            throw Types::DecodeError{"too small LC_UUID command size: {}", lc.C.cmdsize};
        }
        memcpy(&result.data, lc.Ptr + sizeof(lc.C), sizeof(result.data));
        return result;
    }
    return std::nullopt;
}

std::vector<Binja::MachO::Segment> DwarfObjectFile::DecodeSegments() const {
    auto *macho = llvm::dyn_cast<llvm::object::MachOObjectFile>(dwarfContext_->getDWARFObj().getFile());
    BDVerify(macho);
    std::vector<Binja::MachO::Segment> result;
    for (auto lc: macho->load_commands()) {
        if (lc.C.cmd != llvm::MachO::LC_SEGMENT_64) {
            continue;
        }
        const auto *cmd = reinterpret_cast<const llvm::MachO::segment_command_64 *>(lc.Ptr);
        MachO::Segment segment;
        segment.name = cmd->segname;
        segment.vaStart = cmd->vmaddr;
        segment.vaLength = cmd->vmsize;
        segment.dataStart = cmd->fileoff;
        segment.dataLength = cmd->filesize;
        // TODO: decode sections and flags
        result.push_back(segment);
    }
    return result;
}
