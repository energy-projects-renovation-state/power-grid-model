// SPDX-FileCopyrightText: 2022 Contributors to the Power Grid Model project <dynamic.grid.calculation@alliander.com>
//
// SPDX-License-Identifier: MPL-2.0

#pragma once
#ifndef POWER_GRID_MODEL_SPARSE_IDX_VECTOR_HPP
#define POWER_GRID_MODEL_SPARSE_IDX_VECTOR_HPP

#include "power_grid_model.hpp"

#include <boost/iterator/iterator_facade.hpp>
#include <boost/iterator/zip_iterator.hpp>
#include <boost/range.hpp>
#include <boost/range/counting_range.hpp>

/*
A data-structure for iterating through the indptr, ie. sparse representation of data.
Indptr can be eg: [0, 3, 6, 7]
This means that:
objects 0, 1, 2 are coupled to index 0
objects 3, 4, 5 are coupled to index 1
objects 6 is coupled to index 2

Another intuitive way to look at this for python developers is like list of lists: [[0, 1, 2], [3, 4, 5], [6]].

DenseIdxVector is a vector of element to group. ie. [0, 1, 1, 4] would denote that [[0], [1, 2], [], [], [3]]
The input, ie. [0, 1, 3] should be strictly increasing

*/

namespace power_grid_model {

class SparseIdxVector {
  private:
    class GroupIterator : public boost::iterator_facade<GroupIterator, Idx, boost::random_access_traversal_tag,
                                                        boost::iterator_range<IdxCount>, Idx> {
      public:
        explicit constexpr GroupIterator(IdxVector const& indptr, Idx group) : indptr_(indptr), group_(group) {}

      private:
        IdxVector const& indptr_;
        Idx group_;
        friend class boost::iterator_core_access;

        boost::iterator_range<IdxCount> dereference() const {
            return boost::counting_range(indptr_[group_], indptr_[group_ + 1]);
        }
        constexpr bool equal(GroupIterator const& other) const { return group_ == other.group_; }
        constexpr void increment() { ++group_; }
        constexpr void decrement() { --group_; }
        constexpr void advance(Idx n) { group_ += n; }
        constexpr Idx distance_to(GroupIterator const& other) const { return other.group_ - group_; }
    };

  public:
    SparseIdxVector() = default;
    explicit SparseIdxVector(IdxVector indptr) : indptr_(indptr.empty() ? IdxVector{0} : indptr) {
        assert(!indptr.empty());
    }

    constexpr auto size() const { return static_cast<Idx>(indptr_.size()) - 1; }
    constexpr auto begin() const { return GroupIterator(indptr_, 0); }
    constexpr auto end() const { return GroupIterator(indptr_, size()); }

    constexpr auto element_size() const { return indptr_.back(); }
    auto get_element_range(Idx group) const {
        return boost::iterator_range<IdxCount>(indptr_[group], indptr_[group + 1]);
    }
    auto get_group(Idx element) const {
        assert(element < element_size());
        return static_cast<Idx>(std::upper_bound(indptr_.begin(), indptr_.end(), element) - indptr_.begin() - 1);
    }

  private:
    IdxVector indptr_;
};

class DenseIdxVector {
  private:
    class GroupIterator : public boost::iterator_facade<GroupIterator, Idx, boost::random_access_traversal_tag,
                                                        boost::iterator_range<IdxCount>, Idx> {
      public:
        explicit constexpr GroupIterator(IdxVector const& dense_vector, Idx const& group)
            : dense_vector_{dense_vector},
              group_{group},
              group_range_{std::equal_range(std::cbegin(dense_vector_), std::cend(dense_vector_), group)} {}

      private:
        using group_iterator = IdxVector::const_iterator;

        IdxVector const& dense_vector_;
        Idx group_;
        std::pair<group_iterator, group_iterator> group_range_;

        friend class boost::iterator_core_access;

        boost::iterator_range<IdxCount> dereference() const {
            return boost::counting_range(static_cast<Idx>(std::distance(std::cbegin(dense_vector_), group_range_.first)),
                                         static_cast<Idx>(std::distance(std::cbegin(dense_vector_), group_range_.second)));
        }
        constexpr bool equal(GroupIterator const& other) const { return group_ == other.group_; }
        constexpr void increment() { advance(1); }
        constexpr void decrement() { advance(-1); }
        constexpr void advance(Idx n) {
            auto const start = n > 0 ? group_range_.second : std::cbegin(dense_vector_);
            auto const stop = n > 0 ? std::cend(dense_vector_) : group_range_.first;

            group_ += n;
            group_range_ = std::equal_range(start, stop, group_);
        }
    };

    constexpr auto group_iterator(Idx group) const { return GroupIterator{dense_vector_, group}; }

  public:
    DenseIdxVector() = default;
    explicit DenseIdxVector(IdxVector dense_vector, Idx groups_size)
        : dense_vector_(std::move(dense_vector)), groups_size_(groups_size) {}

    constexpr auto size() const { return groups_size_; }
    constexpr auto begin() const { return group_iterator(Idx{}); }
    constexpr auto end() const { return group_iterator(size()); }

    constexpr auto element_size() const { return dense_vector_.size(); }
    constexpr auto get_group(Idx element) const { return dense_vector_[element]; }
    auto get_element_range(Idx group) const { return *group_iterator(group); }

  private:
    IdxVector dense_vector_;
    Idx groups_size_;
};

template <typename T>
concept grouped_idx_vector_type =
    std::is_same<T, const SparseIdxVector>::value || std::is_same<T, const DenseIdxVector>::value;

template <grouped_idx_vector_type First, grouped_idx_vector_type... Rest>
auto zip_sequence(First& first, Rest&... rest) {
    auto all_equal_sizes = ((first.size() == rest.size()) && ...);
    assert(all_equal_sizes);
    auto zip_begin = boost::make_zip_iterator(boost::make_tuple(first.begin(), rest.begin()...));
    auto zip_end = boost::make_zip_iterator(boost::make_tuple(first.end(), rest.end()...));
    return boost::make_iterator_range(zip_begin, zip_end);
}

} // namespace power_grid_model

#endif