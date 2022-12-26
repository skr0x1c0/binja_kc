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


#include <binja/utils/debug.h>
#include <binja/utils/log.h>

#include "slider.h"

using namespace Binja;
using namespace DebugInfo;

void AddressSlider::Map(AddressSlider::Interval from, AddressSlider::Interval to) {
    BDVerify(from.upper() - from.lower() == to.upper() - to.lower());
    BDVerify(from.upper() - from.lower() > 0);
    BDVerify(s1map_.find(from) == s1map_.end());
    BDVerify(s2map_.find(to) == s2map_.end());
    s1map_.insert(from, to.lower());
    s2map_.insert(to, from.lower());
}

std::optional<uint64_t> AddressSlider::SlideAddress(uint64_t address) {
    auto it = s1map_.find(address);
    if (it == s1map_.end()) {
        return std::nullopt;
    }
    return it->second + address - it->first.lower();
}

AddressSlider AddressSlider::CreateFromMachOSegments(const std::vector<MachO::Segment> &from,
                                                     const std::vector<MachO::Segment> &to) {
    AddressSlider slider;
    for (const auto &targetSegment: to) {
        if (!targetSegment.vaLength) {
            BDLogDebug("skipping binary segment {} with no VA", targetSegment.name);
            continue;
        }
        auto sourceSegmentIt = std::find_if(from.begin(), from.end(), [&targetSegment](const auto &sourceSegment) {
            return sourceSegment.name == targetSegment.name;
        });
        if (sourceSegmentIt == from.end()) {
            BDLogDebug("binary segment {} did not match with any segment in symbol",
                       targetSegment.name);
            continue;
        }
        if (!sourceSegmentIt->vaLength) {
            BDLogDebug("symbol segment {} had zero VA length", targetSegment.name);
            continue;
        }
        size_t vaLength = std::min(sourceSegmentIt->vaLength, targetSegment.vaLength);
        AddressSlider::Interval sourceAddressRange{
            sourceSegmentIt->vaStart, sourceSegmentIt->vaStart + vaLength};
        AddressSlider::Interval destAddressRange{
            targetSegment.vaStart, targetSegment.vaStart + vaLength};
        if (sourceSegmentIt->vaLength != targetSegment.vaLength) {
            BDLogWarn("va range trimmed due to length mismatch at segment {} [{:#016x}, {:#016x})->[{:#016x}, {:#016x})",
                      targetSegment.name, sourceAddressRange.lower(), sourceAddressRange.upper(),
                      destAddressRange.lower(), destAddressRange.upper());
        }
        BDLogDebug("mapping segment {}", targetSegment.name);
        slider.Map(sourceAddressRange, destAddressRange);
    }
    return slider;
}
