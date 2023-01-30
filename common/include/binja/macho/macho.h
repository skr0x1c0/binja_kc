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

#include <span>
#include <string>

#include <binaryninjaapi.h>
#include <binaryninjacore.h>

#include "../types/errors.h"
#include "../types/uuid.h"
#include "../utils/binary_view.h"


namespace Binja::MachO {

class DataReaderError : public Types::DecodeError {
    using Types::DecodeError::DecodeError;
};


class MachDataBackend {
public:
    virtual ~MachDataBackend() = default;
    virtual size_t GetStart() const = 0;
    virtual size_t GetLength() const = 0;
    virtual size_t Read(void *buffer, size_t offset, size_t length) const = 0;
};


class MachBinaryViewDataBackend : public MachDataBackend {
public:
    explicit MachBinaryViewDataBackend(BinaryNinja::BinaryView &base) : base_(base) {}

    size_t GetStart() const override {
        return base_.GetStart();
    }

    size_t GetLength() const override {
        return base_.GetLength();
    }

    size_t Read(void *buffer, size_t offset, size_t length) const override {
        return base_.Read(buffer, offset, length);
    }

private:
    BinaryNinja::BinaryView &base_;
};


class MachSpanDataBackend : public MachDataBackend {
public:
    explicit MachSpanDataBackend(const std::span<char>& base) : base_{base} {}

    size_t GetStart() const override {
        return 0;
    }

    size_t GetLength() const override {
        return base_.size();
    }

    size_t Read(void *buffer, size_t offset, size_t length) const override {
        if (offset >= base_.size()) {
            return 0;
        }
        length = std::min(length, base_.size() - offset);
        std::memcpy(buffer, base_.data() + offset, length);
        return length;
    }

private:
    const std::span<char>& base_;
};

}// namespace Binja::MachO


namespace Binja::MachO::Detail {


class DataReader {
public:
    explicit DataReader(const MachDataBackend *base, uint64_t offset)
        : base_{base}, offset_{offset} {}

    template<class T>
    T Read() {
        T result = Peek<T>();
        offset_ += sizeof(T);
        return result;
    }

    template<class T>
    T Peek() {
        T result;
        auto size = sizeof(T);
        auto read = base_->Read(&result, offset_, size);
        if (read != size) {
            throw DataReaderError{"Failed to read data of size {} at offset {}, read only {} bytes", size, offset_, read};
        }
        return result;
    }

    std::string ReadString(size_t maxLength = 1024) {
        size_t length = FindStringLength(maxLength);
        std::string result;
        result.resize(length);
        auto read = base_->Read(result.data(), offset_, length);
        BDVerify(read == length);
        offset_ += length;
        return result;
    }

    void Seek(size_t length) {
        offset_ += length;
        if (offset_ > base_->GetStart() + base_->GetLength()) {
            throw DataReaderError{"Attempt to seek to position {} past EOF, file size: {}", offset_, base_->GetLength()};
        }
    }

    const uint64_t Offset() const {
        return offset_;
    }

private:
    size_t FindStringLength(size_t maxLength) {
        char buffer[32];
        for (size_t cursor = 0; cursor < maxLength; cursor += sizeof(buffer)) {
            size_t read = base_->Read(buffer, offset_ + cursor, sizeof(buffer));
            for (size_t i = 0; i < read; ++i) {
                if (buffer[i] == '\0') {
                    return cursor + i;
                }
            }
            if (read != sizeof(buffer)) {
                throw DataReaderError{"Failed to read string at offset {}, reached EOF at {}", offset_, cursor + read};
            }
        }
        throw DataReaderError{"Failed to read string at offset {}, string exceeds max length {}", offset_, maxLength};
    }

private:
    const MachDataBackend *base_;
    uint64_t offset_;
};

}// namespace Binja::MachO::Detail

namespace Binja::MachO {

class MachHeaderDecodeError : public Types::DecodeError {
    using Types::DecodeError::DecodeError;
};

struct Fileset {
    std::string name;
    uint64_t vmAddr;
    uint64_t fileOffset;
};

struct Section {
    std::string name;
    uint64_t vaStart;
    uint64_t vaLength;
    BNSectionSemantics semantics;
};

struct Segment {
    std::string name;
    uint64_t vaStart;
    uint64_t vaLength;
    uint64_t dataStart;
    uint64_t dataLength;
    uint32_t flags;
    std::vector<Section> sections;
};

struct Symbol {
    std::string name;
    uint64_t addr;
};

struct DyldChainedPtr {
    uint64_t fileOffset;
    uint64_t value;
};

class MachHeaderParser {
public:
    MachHeaderParser(const MachDataBackend &data, uint64_t machHeaderOffset)
        : data_{data}, machHeaderOffset_{machHeaderOffset} {
        VerifyHeader();
    }

    std::vector<Fileset> DecodeFilesets();
    std::vector<Segment> DecodeSegments();
    std::optional<uint64_t> DecodeEntryPoint();
    std::optional<Types::UUID> DecodeUUID();
    std::vector<Symbol> DecodeSymbols();
    std::vector<DyldChainedPtr> DecodeDyldChainedPtrs();

private:
    void VerifyHeader();
    template<class T> std::optional<T> FindCommand(uint32_t cmd);
    std::optional<uint64_t> FindVMBase();

    static Fileset DecodeFileset(Detail::DataReader &reader);
    static Segment DecodeSegment(Detail::DataReader &reader);
    static std::vector<Section> DecodeSections(Detail::DataReader &reader);

private:
    const MachDataBackend &data_;
    uint64_t machHeaderOffset_;
};


class MachBinaryView {
public:
    MachBinaryView(BinaryNinja::BinaryView &binaryView) : binaryView_{binaryView} {}
    std::map<Types::UUID, std::vector<Segment>> ReadMachOHeaders();
    std::vector<uint64_t> ReadMachOHeaderOffsets();

private:
    BinaryNinja::BinaryView &binaryView_;
};

}// namespace Binja::MachO
