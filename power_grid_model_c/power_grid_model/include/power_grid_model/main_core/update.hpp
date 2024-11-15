// SPDX-FileCopyrightText: Contributors to the Power Grid Model project <powergridmodel@lfenergy.org>
//
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "state.hpp"
#include "utils.hpp"

#include "../all_components.hpp"
#include "../common/iterator_like_concepts.hpp"
#include "../container.hpp"

#include <map>

namespace power_grid_model::main_core {

namespace detail {
template <component_c Component, forward_iterator_like<typename Component::UpdateType> ForwardIterator, typename Func>
    requires std::invocable<std::remove_cvref_t<Func>, typename Component::UpdateType, Idx2D const&>
inline void iterate_component_sequence(Func&& func, ForwardIterator begin, ForwardIterator end,
                                       std::span<Idx2D const> sequence_idx) {
    assert(std::distance(begin, end) >= static_cast<ptrdiff_t>(sequence_idx.size()));

    Idx seq = 0;

    // loop to to update component
    for (auto it = begin; it != end; ++it, ++seq) {
        // get component directly using sequence id
        func(*it, sequence_idx[seq]);
    }
}

} // namespace detail

template <component_c Component, class ComponentContainer,
          forward_iterator_like<typename Component::UpdateType> ForwardIterator,
          std::output_iterator<Idx2D> OutputIterator>
    requires model_component_state_c<MainModelState, ComponentContainer, Component>
inline void get_component_sequence(MainModelState<ComponentContainer> const& state, ForwardIterator begin,
                                   ForwardIterator end, OutputIterator destination, Idx n_comp_elements) {
    using UpdateType = typename Component::UpdateType;

    if (n_comp_elements < 0) {
        std::ranges::transform(begin, end, destination, [&state](UpdateType const& update) {
            return get_component_idx_by_id<Component>(state, update.id);
        });
    } else {
        assert(std::distance(begin, end) <= n_comp_elements);
        std::ranges::transform(
            begin, end, destination,
            [group = get_component_group_idx<Component>(state), index = 0](auto const& /*update*/) mutable {
                return Idx2D{group, index++}; // NOSONAR
            });
    }
}

template <component_c Component, class ComponentContainer,
          forward_iterator_like<typename Component::UpdateType> ForwardIterator>
    requires model_component_state_c<MainModelState, ComponentContainer, Component>
inline std::vector<Idx2D> get_component_sequence(MainModelState<ComponentContainer> const& state, ForwardIterator begin,
                                                 ForwardIterator end, Idx n_comp_elements = na_Idx) {
    std::vector<Idx2D> result;
    result.reserve(std::distance(begin, end));
    get_component_sequence<Component>(state, begin, end, std::back_inserter(result), n_comp_elements);
    return result;
}

// template to update components
// using forward interators
// different selection based on component type
// if sequence_idx is given, it will be used to load the object instead of using IDs via hash map.
template <component_c Component, class ComponentContainer,
          forward_iterator_like<typename Component::UpdateType> ForwardIterator,
          std::output_iterator<Idx2D> OutputIterator>
    requires model_component_state_c<MainModelState, ComponentContainer, Component>
inline UpdateChange update_component(MainModelState<ComponentContainer>& state, ForwardIterator begin,
                                     ForwardIterator end, OutputIterator changed_it,
                                     std::span<Idx2D const> sequence_idx) {
    using UpdateType = typename Component::UpdateType;

    UpdateChange state_changed;

    detail::iterate_component_sequence<Component>(
        [&state_changed, &changed_it, &state](UpdateType const& update_data, Idx2D const& sequence_single) {
            auto& comp = get_component<Component>(state, sequence_single);
            assert(state.components.get_id_by_idx(sequence_single) == comp.id());
            auto const comp_changed = comp.update(update_data);
            state_changed = state_changed || comp_changed;

            if (comp_changed.param || comp_changed.topo) {
                *changed_it++ = sequence_single;
            }
        },
        begin, end, sequence_idx);

    return state_changed;
}
template <component_c Component, class ComponentContainer,
          forward_iterator_like<typename Component::UpdateType> ForwardIterator,
          std::output_iterator<Idx2D> OutputIterator>
    requires model_component_state_c<MainModelState, ComponentContainer, Component>
inline UpdateChange update_component(MainModelState<ComponentContainer>& state, ForwardIterator begin,
                                     ForwardIterator end, OutputIterator changed_it) {
    return update_component<Component>(state, begin, end, changed_it,
                                       get_component_sequence<Component>(state, begin, end));
}

// template to get the inverse update for components
// using forward interators
// different selection based on component type
// if sequence_idx is given, it will be used to load the object instead of using IDs via hash map.
template <component_c Component, class ComponentContainer,
          forward_iterator_like<typename Component::UpdateType> ForwardIterator,
          std::output_iterator<typename Component::UpdateType> OutputIterator>
    requires model_component_state_c<MainModelState, ComponentContainer, Component>
inline void update_inverse(MainModelState<ComponentContainer> const& state, ForwardIterator begin, ForwardIterator end,
                           OutputIterator destination, std::span<Idx2D const> sequence_idx) {
    using UpdateType = typename Component::UpdateType;

    detail::iterate_component_sequence<Component>(
        [&destination, &state](UpdateType const& update_data, Idx2D const& sequence_single) {
            auto const& comp = get_component<Component>(state, sequence_single);
            *destination++ = comp.inverse(update_data);
        },
        begin, end, sequence_idx);
}
template <component_c Component, class ComponentContainer,
          forward_iterator_like<typename Component::UpdateType> ForwardIterator,
          std::output_iterator<typename Component::UpdateType> OutputIterator>
    requires model_component_state_c<MainModelState, ComponentContainer, Component>
inline void update_inverse(MainModelState<ComponentContainer> const& state, ForwardIterator begin, ForwardIterator end,
                           OutputIterator destination) {
    return update_inverse<Component>(state, begin, end, destination,
                                     get_component_sequence<Component>(state, begin, end));
}

namespace update_independence {
namespace detail {

constexpr Idx invalid_index{-1};

template <typename T> bool check_id_na(T const& obj) {
    if constexpr (requires { obj.id; }) {
        return is_nan(obj.id);
    } else if constexpr (requires { obj.get().id; }) {
        return is_nan(obj.get().id);
    } else {
        throw UnreachableHit{"check_component_independence", "Only components with id are supported"};
    }
}

struct UpdateCompProperties {
    std::string name{};
    bool has_any_elements{false};             // whether the component has any elements in the update data
    bool ids_all_na{false};                   // whether all ids are all NA
    bool ids_part_na{false};                  // whether some ids are NA but some are not
    bool dense{false};                        // whether the component is dense
    bool uniform{false};                      // whether the component is uniform
    bool is_columnar{false};                  // whether the component is columnar
    bool update_ids_match{false};             // whether the ids match
    Idx elements_ps_in_update{invalid_index}; // count of elements for this component per scenario in update
    Idx elements_in_base{invalid_index};      // count of elements for this component per scenario in input

    constexpr bool no_id() const { return !has_any_elements || ids_all_na; }
    constexpr bool qualify_for_optional_id() const {
        return update_ids_match && ids_all_na && uniform && elements_ps_in_update == elements_in_base;
    }
    constexpr bool provided_ids_valid() const {
        return is_empty_component() || (update_ids_match && !(ids_all_na || ids_part_na));
    }
    constexpr bool is_empty_component() const { return !has_any_elements; }
    constexpr bool is_independent() const { return qualify_for_optional_id() || provided_ids_valid(); }
    constexpr Idx get_n_elements() const {
        assert(uniform || elements_ps_in_update == invalid_index);

        return qualify_for_optional_id() ? elements_ps_in_update : na_Idx;
    }
};

template <typename CompType> void process_buffer_span(auto const& all_spans, detail::UpdateCompProperties& properties) {
    properties.ids_all_na = std::ranges::all_of(all_spans, [](auto const& vec) {
        return std::ranges::all_of(vec, [](auto const& item) { return detail::check_id_na(item); });
    });
    properties.ids_part_na = std::ranges::any_of(all_spans,
                                                 [](auto const& vec) {
                                                     return std::ranges::any_of(vec, [](auto const& item) {
                                                         return detail::check_id_na(item);
                                                     });
                                                 }) &&
                             !properties.ids_all_na;

    if (all_spans.empty()) {
        properties.update_ids_match = true;
    } else {
        // Remember the begin iterator of the first scenario, then loop over the remaining scenarios and
        // check the ids
        auto const first_span = all_spans[0];
        // check the subsequent scenarios
        // only return true if all scenarios match the ids of the first batch
        properties.update_ids_match =
            std::ranges::all_of(all_spans.cbegin() + 1, all_spans.cend(), [&first_span](auto const& current_span) {
                return std::ranges::equal(
                    current_span, first_span,
                    [](typename CompType::UpdateType const& obj, typename CompType::UpdateType const& first) {
                        return obj.id == first.id;
                    });
            });
    }
}

template <class CompType>
detail::UpdateCompProperties check_component_independence(ConstDataset const& update_data, Idx n_component) {
    detail::UpdateCompProperties properties;
    properties.name = CompType::name;
    auto const component_idx = update_data.find_component(properties.name, false);
    properties.is_columnar = update_data.is_columnar(properties.name);
    properties.dense = update_data.is_dense(properties.name);
    properties.uniform = update_data.is_uniform(properties.name);
    properties.has_any_elements =
        component_idx != invalid_index && update_data.get_component_info(component_idx).total_elements > 0;
    properties.elements_ps_in_update =
        properties.uniform ? update_data.uniform_elements_per_scenario(properties.name) : invalid_index;
    properties.elements_in_base = n_component;

    if (properties.is_columnar) {
        detail::process_buffer_span<CompType>(
            update_data.template get_columnar_buffer_span_all_scenarios<meta_data::update_getter_s, CompType>(),
            properties);
    } else {
        detail::process_buffer_span<CompType>(
            update_data.template get_buffer_span_all_scenarios<meta_data::update_getter_s, CompType>(), properties);
    }

    return properties;
}

inline void validate_update_data_independence(detail::UpdateCompProperties const& comp) {
    if (comp.is_empty_component()) {
        return; // empty dataset is still supported
    }
    auto const elements_ps = comp.get_n_elements();
    assert(comp.uniform || elements_ps < 0);

    if (elements_ps >= 0 && comp.elements_in_base < elements_ps) {
        throw DatasetError("Update data has more elements per scenario than input data for component " + comp.name +
                           "!");
    }
    if (comp.ids_part_na) {
        throw DatasetError("Some IDs are not valid for component " + comp.name + " in update data!");
    }
    if (comp.ids_all_na && comp.elements_in_base != elements_ps) {
        throw DatasetError("Update data without IDs for component " + comp.name +
                           " has a different number of elements per scenario then input data!");
    }
}

} // namespace detail

// main core utils
template <class CompType, class... ComponentTypes>
constexpr auto index_of_component = main_core::utils::index_of_component<CompType, ComponentTypes...>;
template <class... ComponentTypes> using SequenceIdx = main_core::utils::SequenceIdx<ComponentTypes...>;
template <class... ComponentTypes> using ComponentFlags = main_core::utils::ComponentFlags<ComponentTypes...>;

// get sequence idx map of a certain batch scenario
template <typename CompType, class ComponentContainer>
std::vector<Idx2D> get_component_sequence(MainModelState<ComponentContainer> const& state,
                                          ConstDataset const& update_data, Idx scenario_idx,
                                          detail::UpdateCompProperties const& comp_independence = {}) {
    auto const get_sequence = [&state, n_comp_elements = comp_independence.get_n_elements()](auto const& span) {
        return get_component_sequence<CompType>(state, std::begin(span), std::end(span), n_comp_elements);
    };
    if (update_data.is_columnar(CompType::name)) {
        auto const buffer_span =
            update_data.get_columnar_buffer_span<meta_data::update_getter_s, CompType>(scenario_idx);
        return get_sequence(buffer_span);
    }
    auto const buffer_span = update_data.get_buffer_span<meta_data::update_getter_s, CompType>(scenario_idx);
    return get_sequence(buffer_span);
}

template <class... ComponentTypes, class ComponentContainer>
SequenceIdx<ComponentTypes...> get_sequence_idx_map(MainModelState<ComponentContainer> const& state,
                                                    ConstDataset const& update_data, Idx scenario_idx,
                                                    ComponentFlags<ComponentTypes...> const& components_to_store) {
    return utils::run_functor_with_all_types_return_array<ComponentTypes...>(
        [&update_data, &components_to_store, &state, scenario_idx]<typename CompType>() {
            if (!std::get<index_of_component<CompType, ComponentTypes...>>(components_to_store)) {
                return std::vector<Idx2D>{};
            }
            auto const n_components = state.components.template size<CompType>();
            auto const independence = detail::check_component_independence<CompType>(update_data, n_components);
            detail::validate_update_data_independence(independence);
            return get_component_sequence<CompType>(state, update_data, scenario_idx, independence);
        });
}
// Get sequence idx map of an entire batch for fast caching of component sequences.
// The sequence idx map of the batch is the same as that of the first scenario in the batch (assuming homogeneity)
// This is the entry point for permanent updates.
template <class... ComponentTypes, class ComponentContainer>
SequenceIdx<ComponentTypes...> get_sequence_idx_map(MainModelState<ComponentContainer> const& state,
                                                    ConstDataset const& update_data) {
    constexpr ComponentFlags<ComponentTypes...> all_true = [] {
        ComponentFlags<ComponentTypes...> result{};
        std::ranges::fill(result, true);
        return result;
    }();
    return get_sequence_idx_map<ComponentTypes...>(state, update_data, 0, all_true);
}

template <class... ComponentTypes>
ComponentFlags<ComponentTypes...> is_update_independent(ConstDataset const& update_data,
                                                        std::span<Idx const> relevant_component_count) {
    ComponentFlags<ComponentTypes...> result{};
    size_t comp_idx{};
    utils::run_functor_with_all_types_return_void<ComponentTypes...>([&result, &relevant_component_count, &update_data,
                                                                      &comp_idx]<typename CompType>() {
        Idx const n_component = relevant_component_count[comp_idx];
        result[comp_idx] = detail::check_component_independence<CompType>(update_data, n_component).is_independent();
        ++comp_idx;
    });
    return result;
}

} // namespace update_independence

} // namespace power_grid_model::main_core
