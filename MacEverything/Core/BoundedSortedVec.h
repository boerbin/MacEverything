#pragma once
#include <vector>
#include <algorithm>
#include <cstddef>
#include <functional>

template <typename T, typename Compare = std::less<T>>
class BoundedSortedVec {
public:
    explicit BoundedSortedVec(size_t maxSize = 100)
        : maxSize_(maxSize) {
        data_.reserve(maxSize);
    }

    bool insert(const T& item) {
        auto it = std::lower_bound(data_.begin(), data_.end(), item, comp_);
        if (data_.size() >= maxSize_) {
            // If item would go at end (worst position) and we're full, reject
            if (it == data_.end()) return false;
            data_.insert(it, item);
            data_.pop_back();
        } else {
            data_.insert(it, item);
        }
        return true;
    }

    bool erase(const T& item) {
        auto it = std::lower_bound(data_.begin(), data_.end(), item, comp_);
        if (it != data_.end() && !comp_(item, *it) && !comp_(*it, item)) {
            data_.erase(it);
            return true;
        }
        return false;
    }

    void clear() { data_.clear(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    bool full() const { return data_.size() >= maxSize_; }
    size_t capacity() const { return maxSize_; }

    const T& operator[](size_t idx) const { return data_[idx]; }
    const T& back() const { return data_.back(); }

    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }
    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }

    const std::vector<T>& data() const { return data_; }
    std::vector<T>& mutableData() { return data_; }

    void reserve(size_t n) { data_.reserve(n); }
    void setMaxSize(size_t n) { maxSize_ = n; }

private:
    std::vector<T> data_;
    size_t maxSize_;
    Compare comp_;
};
