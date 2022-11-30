

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
#include <binaryninjacore.h>
#include <fmt/format.h>

#include "dwarf.h"
#include "errors.h"

#define DWARF_DEBUG_BUILD 1

#define VerifyNotReachable() \
    throw Binja::DebugInfo::FatalError { "VerifyNotReachable failed at {}:{}", __FILE__, __LINE__ }

#define Verify(Condition, ErrorType)                               \
    if (!(Condition)) {                                            \
        throw ErrorType{"Verify condition {} failed", #Condition}; \
    }                                                              \
    0

#if DWARF_DEBUG_BUILD
#define DebugVerify(Condition, ErrorType)                            \
    if (!(Condition)) {                                              \
        throw ErrorType{"Debug verify condition {} failed at {}:{}", \
                        #Condition, __FILE_NAME__, __LINE__};        \
    }                                                                \
    0
#else
#define DebugVerify(Condition, ErrorType) \
    do {                                  \
    } while (0)
#endif

#define Todo() \
    throw Binja::Dwarf::FatalError { "todo at {}:{}", __FILE__, __LINE__ }

#define VerifyDumpDie(Condition, Die)                                                        \
    if (!(Condition)) {                                                                      \
        throw Binja::DebugInfo::DwarfError{"Verify condition {} failed at {}:{} for DIE {}", \
                                           #Condition, __FILE_NAME__, __LINE__,              \
                                           Binja::DebugInfo::DieReader{Die}.Dump()};         \
    }                                                                                        \
    0

#if DWARF_DEBUG_BUILD
#define VerifyDebugDumpDie(Condition, Die)                                                         \
    if (!(Condition)) {                                                                            \
        throw Binja::DebugInfo::DwarfError{"Debug verify condition {} failed at {}:{} for DIE {}", \
                                           #Condition, __FILE_NAME__, __LINE__,                    \
                                           Binja::DebugInfo::DieReader{Die}.Dump()};               \
    }                                                                                              \
    0
#else
#define VerifyDebugDumpDie(Condition, Die)                           \
    if (!(Condition)) {                                              \
        Binja::Dwarf::AttributeReader reader{Die};                   \
        throw LogWarn{"Debug verify condition {} failed for DIE {}", \
                      #Condition, reader.DebugInfo()};               \
    }
#endif