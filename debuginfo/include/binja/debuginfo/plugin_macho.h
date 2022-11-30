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

#include <filesystem>

#include <binaryninjaapi.h>

#include "dsym.h"
#include "macho_task.h"

namespace Binja::DebugInfo {

class PluginMacho {
public:
    static constexpr auto kPluginName = "macho_debug_info";

public:
    PluginMacho(BinaryNinja::BinaryView &binaryView)
        : binaryView_{binaryView} {}

    void Load(BinaryNinja::DebugInfo &debugInfo, MachOImportProgressMonitor &monitor);

    static void RegisterPlugin();
    std::optional<std::filesystem::path> GetSymbolSource();

private:
    BinaryNinja::BinaryView &binaryView_;
};

}// namespace Binja::DebugInfo