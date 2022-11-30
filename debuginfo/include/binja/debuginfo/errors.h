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

#include <exception>
#include <fmt/format.h>

namespace Binja::DebugInfo {

class GenericException : public std::exception {
public:
    template<typename... T>
    explicit GenericException(fmt::format_string<T...> fmt, T &&...args)
        : msg_{fmt::format(fmt, std::forward<T>(args)...)} {}
    [[nodiscard]] const char *what() const noexcept override { return msg_.c_str(); }

private:
    std::string msg_;
};

class FatalError : public GenericException {
    using GenericException::GenericException;
};

class DwarfError : public GenericException {
    using GenericException::GenericException;
};

void test(const std::string &path);
}// namespace Binja::DebugInfo
