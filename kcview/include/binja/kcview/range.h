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
//


#pragma once

#include <binja/utils/debug.h>
#include <binja/utils/interval_map.h>

namespace Binja::KCView {

template<class K, class V>
class RangeMap {
public:
    void Insert(Utils::Interval<K> key, V value) {
        auto it = index_.find(key);
        BDVerify(it == index_.end());
        auto idx = values_.size();
        values_.push_back(value);
        index_.insert(key, idx);
    }

    const V *Query(K key) {
        return QueryInternal(key);
    }

    const V *Query(Utils::Interval<K> key) {
        return QueryInternal(key);
    }

    K FindNextValid(K key) {
        BDVerify(Query(key) == nullptr);
        for (auto it = index_.begin(), end = index_.end(); it != end; ++it) {
            if (it->first.lower() > key) {
                return it->first.lower();
            }
        }
        return 0;
    }

    const std::vector<V> &Values() {
        return values_;
    }

private:
    template<class T>
    const V *QueryInternal(T key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        return &values_[it->second];
    }

    Utils::IntervalMap<K, size_t> index_;
    std::vector<V> values_;
};

}// namespace Binja::KCView