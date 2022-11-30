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

#include <boost/icl/interval_map.hpp>

#include <binja/macho/macho.h>

namespace Binja::DebugInfo {

class AddressSlider {
private:
    using Interval = boost::icl::discrete_interval<uint64_t>;

public:
    void Map(Interval from, Interval to);
    std::optional<uint64_t> SlideAddress(uint64_t address);

    static AddressSlider CreateFromMachOSegments(const std::vector<MachO::Segment> &from,
                                                 const std::vector<MachO::Segment> &to);

private:
    boost::icl::interval_map<uint64_t, uint64_t> s1map_;
    boost::icl::interval_map<uint64_t, uint64_t> s2map_;
};

}// namespace Binja::DebugInfo