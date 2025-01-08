// SPDX-FileCopyrightText: Contributors to the Power Grid Model project <powergridmodel@lfenergy.org>
//
// SPDX-License-Identifier: MPL-2.0

#include <power_grid_model/component/current_sensor.hpp>

#include <doctest/doctest.h>

namespace power_grid_model {
namespace {
auto const r_nan = RealValue<asymmetric_t>{nan};

void check_nan_preserving_equality(std::floating_point auto actual, std::floating_point auto expected) {
    if (is_nan(expected)) {
        is_nan(actual);
    } else {
        CHECK(actual == doctest::Approx(expected));
    }
}
void check_nan_preserving_equality(RealValue<asymmetric_t> const& actual, RealValue<asymmetric_t> const& expected) {
    for (auto i : {0, 1, 2}) {
        CAPTURE(i);
        check_nan_preserving_equality(actual(i), expected(i));
    }
}
} // namespace

TEST_CASE("Test current sensor") {
    SUBCASE("Symmetric Current Sensor") {
        for (auto const terminal_type :
             {MeasuredTerminalType::branch_from, MeasuredTerminalType::branch_to, MeasuredTerminalType::branch3_1,
              MeasuredTerminalType::branch3_2, MeasuredTerminalType::branch3_3}) {
            CAPTURE(terminal_type);

            CurrentSensorInput<symmetric_t> sym_current_sensor_input{};
            sym_current_sensor_input.id = 0;
            sym_current_sensor_input.measured_object = 1;
            sym_current_sensor_input.measured_terminal_type = terminal_type;
            sym_current_sensor_input.angle_measurement_type = AngleMeasurementType::local;
            sym_current_sensor_input.i_sigma = 1.0;
            sym_current_sensor_input.i_measured = 1.0 * 1e3;
            sym_current_sensor_input.i_angle_measured = 0.0;
            sym_current_sensor_input.i_angle_sigma = nan;

            double const u_rated = 10.0e3;
            double const base_current = base_power_3p / u_rated / sqrt3;

            ComplexValue<symmetric_t> const i_sym = (1.0 * 1e3 + 1i * 0.0) / base_current;
            ComplexValue<asymmetric_t> const i_asym = i_sym * RealValue<asymmetric_t>{1.0};

            CurrentSensor<symmetric_t> sym_current_sensor{sym_current_sensor_input, u_rated};

            CurrentSensorCalcParam<symmetric_t> sym_sensor_param = sym_current_sensor.calc_param<symmetric_t>();
            CurrentSensorCalcParam<asymmetric_t> asym_sensor_param = sym_current_sensor.calc_param<asymmetric_t>();

            CurrentSensorOutput<symmetric_t> sym_sensor_output = sym_current_sensor.get_output<symmetric_t>(i_sym);
            CurrentSensorOutput<asymmetric_t> sym_sensor_output_asym_param =
                sym_current_sensor.get_output<asymmetric_t>(i_asym);

            // Check symmetric sensor output for symmetric parameters
            CHECK(sym_sensor_param.angle_measurement_type == AngleMeasurementType::local);
            CHECK(sym_sensor_param.i_variance == doctest::Approx(0.0));
            CHECK(sym_sensor_param.i_angle_variance == doctest::Approx(0.0));
            CHECK(real(sym_sensor_param.value) == doctest::Approx(0.0));
            CHECK(imag(sym_sensor_param.value) == doctest::Approx(0.0));

            CHECK(is_nan(sym_sensor_output.id));
            CHECK(is_nan(sym_sensor_output.energized));
            CHECK(is_nan(sym_sensor_output.i_residual));
            CHECK(is_nan(sym_sensor_output.i_angle_residual));

            // Check symmetric sensor output for asymmetric parameters
            CHECK(asym_sensor_param.i_variance == doctest::Approx(0.0));
            CHECK(asym_sensor_param.i_angle_variance == doctest::Approx(0.0));
            CHECK(real(asym_sensor_param.value[0]) == doctest::Approx(0.0));
            CHECK(imag(asym_sensor_param.value[1]) == doctest::Approx(0.0));

            CHECK(is_nan(sym_sensor_output_asym_param.id));
            CHECK(is_nan(sym_sensor_output_asym_param.energized));
            CHECK(is_nan(sym_sensor_output_asym_param.i_residual[0]));
            CHECK(is_nan(sym_sensor_output_asym_param.i_angle_residual[1]));

            CHECK(sym_current_sensor.get_terminal_type() == terminal_type);

            CHECK(sym_current_sensor.get_angle_measurement_type() == AngleMeasurementType::local);
        }
        SUBCASE("Wrong measured terminal type") {
            for (auto const terminal_type :
                 {MeasuredTerminalType::source, MeasuredTerminalType::shunt, MeasuredTerminalType::load,
                  MeasuredTerminalType::generator, MeasuredTerminalType::node}) {
                CHECK_THROWS_AS((CurrentSensor<symmetric_t>{
                                    {1, 1, terminal_type, AngleMeasurementType::local, 1.0, 1.0, 1.0, 1.0}, 1.0}),
                                InvalidMeasuredTerminalType);
            }
        }
    }
    SUBCASE("Update inverse - sym") {
        constexpr auto i_measured = 1.0;
        constexpr auto i_angle_measured = 2.0;
        constexpr auto i_sigma = 3.0;
        constexpr auto i_angle_sigma = 4.0;
        constexpr auto u_rated = 10.0e3;
        CurrentSensor<symmetric_t> const current_sensor{{1, 1, MeasuredTerminalType::branch3_1,
                                                         AngleMeasurementType::local, i_sigma, i_angle_sigma,
                                                         i_measured, i_angle_measured},
                                                        u_rated};

        CurrentSensorUpdate<symmetric_t> cs_update{1, nan, nan, nan, nan};
        auto expected = cs_update;

        SUBCASE("Identical") {
            // default values
        }

        SUBCASE("i_sigma") {
            SUBCASE("same") { cs_update.i_sigma = i_sigma; }
            SUBCASE("different") { cs_update.i_sigma = 0.0; }
            expected.i_sigma = i_sigma;
        }

        SUBCASE("i_angle_sigma") {
            SUBCASE("same") { cs_update.i_angle_sigma = i_angle_sigma; }
            SUBCASE("different") { cs_update.i_angle_sigma = 0.0; }
            expected.i_angle_sigma = i_angle_sigma;
        }

        SUBCASE("i_measured") {
            SUBCASE("same") { cs_update.i_measured = i_measured; }
            SUBCASE("different") { cs_update.i_measured = 0.0; }
            expected.i_measured = i_measured;
        }

        SUBCASE("i_angle_measured") {
            SUBCASE("same") { cs_update.i_angle_measured = i_angle_measured; }
            SUBCASE("different") { cs_update.i_angle_measured = 0.0; }
            expected.i_angle_measured = i_angle_measured;
        }

        SUBCASE("multiple") {
            cs_update.i_sigma = 0.0;
            cs_update.i_angle_sigma = 0.0;
            cs_update.i_measured = 0.0;
            cs_update.i_angle_measured = 0.0;
            expected.i_sigma = i_sigma;
            expected.i_angle_sigma = i_angle_sigma;
            expected.i_measured = i_measured;
            expected.i_angle_measured = i_angle_measured;
        }

        auto const inv = current_sensor.inverse(cs_update);

        CHECK(inv.id == expected.id);
        check_nan_preserving_equality(inv.i_sigma, expected.i_sigma);
        check_nan_preserving_equality(inv.i_angle_sigma, expected.i_angle_sigma);
        check_nan_preserving_equality(inv.i_measured, expected.i_measured);
        check_nan_preserving_equality(inv.i_angle_measured, expected.i_angle_measured);
    }

    SUBCASE("Update inverse - asym") {
        RealValue<asymmetric_t> i_measured = {1.0, 2.0, 3.0};
        RealValue<asymmetric_t> i_angle_measured = {4.0, 5.0, 6.0};
        constexpr auto i_sigma = 3.0;
        constexpr auto i_angle_sigma = 4.0;
        constexpr auto u_rated = 10.0e3;
        MeasuredTerminalType const measured_terminal_type = MeasuredTerminalType::branch_from;

        CurrentSensorUpdate<asymmetric_t> cs_update{1, nan, nan, r_nan, r_nan};
        auto expected = cs_update;

        SUBCASE("Identical") {
            // default values
        }

        SUBCASE("i_sigma") {
            SUBCASE("same") { cs_update.i_sigma = i_sigma; }
            SUBCASE("different") { cs_update.i_sigma = 0.0; }
            expected.i_sigma = i_sigma;
        }

        SUBCASE("i_angle_sigma") {
            SUBCASE("same") { cs_update.i_angle_sigma = i_angle_sigma; }
            SUBCASE("different") { cs_update.i_angle_sigma = 0.0; }
            expected.i_angle_sigma = i_angle_sigma;
        }

        SUBCASE("i_measured") {
            SUBCASE("same") { cs_update.i_measured = i_measured; }
            SUBCASE("1 different") {
                cs_update.i_measured = {0.0, nan, nan};
                expected.i_measured = {i_measured(0), nan, nan};
            }
            SUBCASE("all different") {
                cs_update.i_measured = {0.0, 0.1, 0.2};
                expected.i_measured = i_measured;
            }
        }

        SUBCASE("i_angle_measured") {
            SUBCASE("same") { cs_update.i_angle_measured = i_angle_measured; }
            SUBCASE("1 different") {
                cs_update.i_angle_measured = {0.0, nan, nan};
                expected.i_angle_measured = {i_angle_measured(0), nan, nan};
            }
            SUBCASE("all different") {
                cs_update.i_angle_measured = {0.0, 0.1, 0.2};
                expected.i_angle_measured = i_angle_measured;
            }
        }

        SUBCASE("multiple") {
            cs_update.i_sigma = 0.0;
            cs_update.i_angle_sigma = 0.1;
            cs_update.i_measured = {0.0, 0.2, 0.4};
            cs_update.i_angle_measured = {0.0, 0.3, 0.6};
            expected.i_sigma = i_sigma;
            expected.i_angle_sigma = i_angle_sigma;
            expected.i_measured = i_measured;
            expected.i_angle_measured = i_angle_measured;
        }

        CurrentSensor<asymmetric_t> const current_sensor{{1, 1, measured_terminal_type, AngleMeasurementType::local,
                                                          i_sigma, i_angle_sigma, i_measured, i_angle_measured},
                                                         u_rated};

        auto const inv = current_sensor.inverse(cs_update);

        CHECK(inv.id == expected.id);
        check_nan_preserving_equality(inv.i_sigma, expected.i_sigma);
        check_nan_preserving_equality(inv.i_angle_sigma, expected.i_angle_sigma);
        check_nan_preserving_equality(inv.i_measured, expected.i_measured);
        check_nan_preserving_equality(inv.i_angle_measured, expected.i_angle_measured);
    }
}

} // namespace power_grid_model
