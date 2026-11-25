#include "physics.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using namespace three_body;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

double max_position_error(const Vectors& computed, const Vectors& exact) {
    double largest = 0.0;
    for (std::size_t i = 0; i < computed.size(); ++i) {
        largest = std::max(largest, norm(computed[i] - exact[i]));
    }
    return largest;
}

void unequal_mass_forces() {
    State s;
    s.G = 2.0;
    s.masses = {2.0, 3.0, 5.0};
    s.positions = {Vec2{0.0, 0.0}, Vec2{1.0, 0.0}, Vec2{0.0, 2.0}};
    const auto a = accelerations(s);
    require(norm(a[0] - Vec2{6.0, 2.5}) < 1e-14, "Inverse-square force or mass scaling");
    const auto total_force = s.masses[0] * a[0] + s.masses[1] * a[1] + s.masses[2] * a[2];
    require(norm(total_force) < 1e-13, "Internal forces do not cancel");
    const auto d = diagnostics(s);
    require(norm(d.center_of_mass - Vec2{0.3, 1.0}) < 1e-14, "Mass-weighted barycenter");
    require(std::abs(d.energy + 12.0 + 10.0 + 30.0 / std::sqrt(5.0)) < 1e-13,
            "Potential energy or pair double-counting");
    require(d.distances[0] == 1.0 && d.distances[2] == 2.0 &&
            std::abs(d.distances[1] - std::sqrt(5.0)) < 1e-14, "Pair-distance order");
}

double circle_error(std::size_t steps) {
    const auto circle = lagrange();
    const auto initial = diagnostics(circle.initial);
    require(std::abs(initial.energy + 1.5) < 1e-14, "Lagrange energy");
    require(std::abs(initial.angular_momentum - std::sqrt(3.0)) < 1e-14, "Lagrange angular momentum");
    double largest = 0.0;
    simulate(circle.initial, circle.period, steps, 1, [&](double t, const State& s) {
        largest = std::max(largest, max_position_error(s.positions, lagrange_positions(t)));
    });
    return largest;
}

void circular_convergence() {
    const double coarse = circle_error(256);
    const double medium = circle_error(512);
    const double fine = circle_error(1024);
    for (double ratio : {coarse / medium, medium / fine}) {
        require(ratio > 3.9 && ratio < 4.1, "Step halving should reduce the trajectory error fourfold");
    }
    require(fine < 6e-5, "Circular trajectory too far from analytic reference");
    std::cout << "Lagrange maximum position errors (256 / 512 / 1024 steps): "
              << coarse << " / " << medium << " / " << fine << '\n';
}

void choreography() {
    const auto orbit = figure_eight();
    const auto initial = diagnostics(orbit.initial);
    double relative_energy_error = 0.0;
    double end_error = std::numeric_limits<double>::infinity();
    bool saw_permutation = false;
    simulate(orbit.initial, orbit.period, 7200, 12, [&](double t, const State& s) {
        const auto d = diagnostics(s);
        relative_energy_error = std::max(relative_energy_error, std::abs((d.energy - initial.energy) / initial.energy));
        require(norm(d.momentum) < 5e-12 && std::abs(d.angular_momentum) < 5e-12 &&
                norm(d.center_of_mass) < 5e-12, "Figure-eight momentum, angular momentum or barycenter");
        if (std::abs(t - orbit.period / 3.0) < 1e-12) {
            // One third of a period permutes the three bodies cyclically.
            saw_permutation = true;
            for (std::size_t i = 0; i < s.positions.size(); ++i) {
                const auto expected = orbit.initial.positions[(i + 2) % 3];
                require(norm(s.positions[i] - expected) < 5e-6, "Incorrect one-third-period choreography");
            }
        }
        if (t == orbit.period) end_error = max_position_error(s.positions, orbit.initial.positions);
    });
    require(saw_permutation, "Missing sample at one third of the period");
    require(relative_energy_error < 2e-6, "Excessive energy drift on figure eight");
    require(end_error < 5e-6, "Figure eight fails to close");
    std::cout << "Figure eight: max relative energy error " << relative_energy_error
              << ", position return error " << end_error << '\n';
}

void sampling_and_validation() {
    const auto orbit = figure_eight();
    std::vector<double> times;
    simulate(orbit.initial, 0.5, 8, 3, [&](double t, const State&) { times.push_back(t); });
    require(times == std::vector<double>{0.0, 0.1875, 0.375, 0.5}, "Output times or missing endpoint");

    State coincident = orbit.initial;
    coincident.positions[1] = coincident.positions[0];
    bool detected = false;
    try {
        simulate(coincident, 1.0, 10, 1, [](double, const State&) {});
    } catch (const std::domain_error&) {
        detected = true;
    }
    require(detected, "Singular configuration must raise an error");

    State invalid = orbit.initial;
    invalid.velocities[0].x = std::numeric_limits<double>::quiet_NaN();
    detected = false;
    try {
        simulate(invalid, 1.0, 10, 1, [](double, const State&) {});
    } catch (const std::invalid_argument&) {
        detected = true;
    }
    require(detected, "Non-finite initial state must raise an error");
}

} // namespace

int main() {
    try {
        unequal_mass_forces();
        circular_convergence();
        choreography();
        sampling_and_validation();
        std::cout << "All physics tests passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << '\n';
        return 1;
    }
}
