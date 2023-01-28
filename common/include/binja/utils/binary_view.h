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

#include <binaryninjaapi.h>

#include "../types/errors.h"
#include "../utils/debug.h"

namespace Binja::Utils {

class BinaryViewReaderError : public Types::DecodeError {
    using Types::DecodeError::DecodeError;
};

class BinaryViewDataReader {
public:
    BinaryViewDataReader(BinaryNinja::BinaryView *base, uint64_t offset)
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
            throw BinaryViewReaderError{"Failed to read data of size {} at offset {}, read only {} bytes", size, offset_, read};
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
            throw BinaryViewReaderError{"Attempt to seek to position {} past EOF, file size: {}", offset_, base_->GetLength()};
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
                throw BinaryViewReaderError{"Failed to read string at offset {}, reached EOF at {}", offset_, cursor + read};
            }
        }
        throw BinaryViewReaderError{"Failed to read string at offset {}, string exceeds max length {}", offset_, maxLength};
    }

private:
    BinaryNinja::BinaryView *base_;
    uint64_t offset_;
};

BinaryNinja::Ref<BinaryNinja::BinaryView> OpenBinaryView(
    const std::string &path, bool updateAnalysis = true,
    BinaryNinja::Ref<BinaryNinja::BinaryViewType> viewType = nullptr,
    std::function<bool(size_t, size_t)> progress = nullptr,
    Json::Value options = Json::Value{});

BinaryNinja::Ref<BinaryNinja::BinaryView> OpenBinaryView(
    BinaryNinja::Ref<BinaryNinja::BinaryView> view, bool updateAnalysis = true,
    BinaryNinja::Ref<BinaryNinja::BinaryViewType> requestedViewType = nullptr,
    std::function<bool(size_t, size_t)> progress = nullptr,
    Json::Value options = Json::Value{}, bool isDatabase = false);

}// namespace Binja::Utils