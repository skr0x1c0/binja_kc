//
// Created by Sreejith Krishnan R on 26/12/22.
//

#pragma once

#include <fmt/format.h>
#include <map>

#include "debug.h"

namespace Binja::Utils {

template<class Domain>
class Interval {
public:
    Interval(Domain start, Domain end) : start_{start}, end_{end} {
        BDVerify(start <= end);
    }

    Domain lower() const {
        return start_;
    }

    Domain upper() const {
        return end_;
    }

    bool overlaps(const Interval<Domain> &oth) const {
        return (start_ >= oth.start_ && start_ < oth.end_)
               || (end_ > oth.start_ && end_ <= oth.end_)
               || (oth.start_ >= start_ && oth.start_ < end_)
               || (oth.end_ > start_ && oth.end_ <= end_);
    }

private:
    Domain start_;
    Domain end_;
};

template<class Domain, class Value>
class IntervalMap {
private:
public:
    void insert(Interval<Domain> interval, Value value) {
        auto entry = entries_.lower_bound(interval);
        if (entry != entries_.end() && entry->first.overlaps(interval)) {
            throw std::range_error{fmt::format("existing interval {} overlaps with provided interval {}",
                                               entry->first, interval)};
        }
        entries_.insert({interval, value});
    }

    auto find(const Domain &key) const {
        if (key + 1 < key) {
            return end();
        }
        Interval interval{key, key + 1};
        return find(interval);
    }

    auto find(const Interval<Domain> &interval) const {
        auto entry = entries_.lower_bound(interval);
        if (entry == entries_.end() || !entry->first.overlaps(interval)) {
            return entries_.end();
        }
        return entry;
    }

    const size_t size() const {
        return entries_.size();
    }

    auto begin() const {
        return entries_.begin();
    }

    auto end() const {
        return entries_.end();
    }

private:
    struct IntervalComparator {
        bool operator()(const Interval<Domain>& i1, const Interval<Domain>& i2) const {
            return i2.lower() < i1.lower();
        }
    };

private:
    std::map<Interval<Domain>, Value, IntervalComparator> entries_;
};

}// namespace Binja::Utils

namespace fmt {

template<class Domain>
struct formatter<Binja::Utils::Interval<Domain>> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const Binja::Utils::Interval<Domain> &p, FormatContext &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "[{},{})", p.lower(), p.upper());
    }
};

}// namespace fmt