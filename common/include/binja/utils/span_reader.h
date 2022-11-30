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

#include "../types/errors.h"

namespace Binja::Utils {

class SpanReader {
public:
    class ReadError : public Types::DecodeError {
        using Types::DecodeError::DecodeError;
    };

public:
    explicit SpanReader(const std::span<const char> &data)
        : image_{data} {}

    template<class T> const T *Read();
    std::string ReadString();
    template<class T> const T *Peek(size_t offset = 0);
    std::string PeekString(size_t offset = 0);
    SpanReader &Skip(size_t size);
    SpanReader Sub(size_t size);

private:
    void VerifyAvailable(uint64_t size);

private:
    const std::span<const char> image_;
    size_t offset_ = 0;
};

template<class T>
inline const T *SpanReader::Read() {
    const T *result = Peek<T>();
    offset_ += sizeof(T);
    return result;
}

template<class T>
inline const T *SpanReader::Peek(size_t offset) {
    VerifyAvailable(offset + sizeof(T));
    const T *result = reinterpret_cast<const T *>(&image_.data()[offset_ + offset]);
    return result;
}

}// namespace Binja::Utils