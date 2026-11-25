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

Vec3 tilt(Vec3 v) {
    return {v.x, 0.6 * v.y - 0.8 * v.z, 0.8 * v.y + 0.6 * v.z};
}

void close_encounter() {
    // Exact eccentric equilateral Lagrange orbit at apoapsis. Its three side
    // lengths fall from 1+e to 1-e in half a period. Rotate the orbit into 3D.
    const auto circle = lagrange();
    const double e = 0.98;
    State start = circle.initial;
    Vectors exact;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3 axis = tilt(start.positions[i]);
        exact[i] = -(1.0 - e) * axis;
        start.positions[i] = (1.0 + e) * axis;
        start.velocities[i] = std::sqrt((1.0 - e) / (1.0 + e)) * tilt(start.velocities[i]);
    }
    double errors[2]{};
    for (std::size_t test = 0; test < 2; ++test) {
        AdaptiveOptions o;
        o.rtol = test == 0 ? 1e-5 : 1e-10;
        o.atol_position = o.atol_velocity = o.rtol * 0.001;
        const auto stats = simulate_adaptive(start, circle.period / 2, 1, o, [&](double t, const State& s) {
            if (t > 0) errors[test] = max_position_error(s.positions, exact);
        });
        require(stats.min_step < 0.01 * stats.max_step, "Steps did not shrink near periapsis");
        require(std::abs(stats.min_distance - (1.0 - e)) < 1e-5, "Close passage was missed");
        require(stats.max_error_ratio <= 1.0, "Uncontrolled trial accepted");
    }
    require(errors[1] < 2e-8 && errors[1] < errors[0] / 25, "Periapsis time or trajectory incorrect");
    std::cout << "Eccentric 3D Lagrange orbit: max position error at periapsis (loose / tight) "
              << errors[0] << " / " << errors[1] << '\n';
    AdaptiveOptions guarded;
    guarded.min_distance = 0.05;
    bool stopped = false;
    try {
        simulate_adaptive(start, circle.period / 2, 1, guarded, [](double, const State&) {});
    } catch (const std::domain_error&) { stopped = true; }
    require(stopped, "Minimum separation did not stop the integration");
}

void spatial_motion() {
    State start;
    start.masses = {0.6, 1.5, 0.9};
    start.positions = {Vec3{1.2, 0, 0}, Vec3{0, -1.1, 0}, Vec3{-.6, .5, 0}};
    start.velocities = {Vec3{0, .5, -.2}, Vec3{-.3, .1, .45}, Vec3{.4, .6, .1}};
    const auto initial = diagnostics(start);
    AdaptiveOptions o;
    o.rtol = 1e-10;
    o.atol_position = o.atol_velocity = 1e-12;
    std::vector<State> frames;
    const double duration = 0.75;
    simulate_adaptive(start, duration, 12, o, [&](double t, const State& s) {
        require(t == duration * (static_cast<double>(frames.size()) / 12), "Nonuniform adaptive output");
        frames.push_back(s);
        const auto d = diagnostics(s);
        require(norm(d.momentum - initial.momentum) < 1e-12, "3D linear momentum drift");
        require(norm(d.angular_momentum_vector - initial.angular_momentum_vector) < 2e-9,
                "3D angular momentum drift");
        require(norm(d.center_of_mass - initial.center_of_mass - (t / 3.0) * initial.momentum) < 1e-12,
                "Incorrect motion of the barycenter");
    });
    require(frames.size() == 13 && std::abs(frames.back().positions[1].z) > .05, "No spatial evolution");

    const Vec3 displacement{128, 32, -64}, boost{-22, 17, 45};
    for (std::size_t i = 0; i < 3; ++i) {
        start.positions[i] = start.positions[i] + displacement;
        start.velocities[i] = start.velocities[i] + boost;
    }
    std::size_t n = 0;
    simulate_adaptive(start, duration, 12, o, [&](double t, const State& s) {
        const auto& original = frames[n++];
        for (std::size_t i = 0; i < 3; ++i) {
            require(norm(s.positions[i] - displacement - t * boost - original.positions[i]) < 2e-12 &&
                    norm(s.velocities[i] - boost - original.velocities[i]) < 2e-12, "Galilean invariance");
        }
    });
}

void failure_limits() {
    const auto start = figure_eight().initial;
    const auto ignore = [](double, const State&) {};
    bool detected = false;
    AdaptiveOptions o;
    o.max_steps = 1;
    try { simulate_adaptive(start, 1.0, 2, o, ignore); }
    catch (const std::runtime_error&) { detected = true; }
    require(detected, "Adaptive integration exceeded its work limit");
    o = AdaptiveOptions{};
    o.min_step = 1.0;
    detected = false;
    try { simulate_adaptive(start, 1.0, 2, o, ignore); }
    catch (const std::underflow_error&) { detected = true; }
    require(detected, "Minimum step was ignored");
    o = AdaptiveOptions{};
    o.atol_velocity = std::numeric_limits<double>::quiet_NaN();
    detected = false;
    try { simulate_adaptive(start, 1.0, 2, o, ignore); }
    catch (const std::invalid_argument&) { detected = true; }
    require(detected, "Non-finite adaptive tolerance");
}

void grazing_passage() {
    // Both endpoints of a single accepted step lie outside radius 1. The
    // almost ballistic path reaches an interior minimum at t ≈ 0.065.
    State s;
    s.G = 1e-14;
    s.positions = {Vec3{0, 0, 0}, Vec3{-.065, .9999, 0}, Vec3{0, 5, 2}};
    s.velocities[1] = {1, 0, 0};
    const auto ignore = [](double, const State&) {};
    AdaptiveOptions o;
    o.min_distance = 0.999;
    auto stats = simulate_adaptive(s, 0.17, 1, o, ignore);
    require(stats.accepted_steps == 1 && std::abs(stats.min_distance - .9999) < 1e-12,
            "Interior distance minimum was missed");
    o.min_distance = 1.0;
    bool detected = false;
    try { simulate_adaptive(s, 0.17, 1, o, ignore); }
    catch (const std::domain_error&) { detected = true; }
    require(detected, "Grazing cutoff crossing between two accepted endpoints was missed");
}

} // namespace

int main() {
    try {
        unequal_mass_forces();
        circular_convergence();
        choreography();
        sampling_and_validation();
        close_encounter();
        spatial_motion();
        failure_limits();
        grazing_passage();
        std::cout << "All physics tests passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << '\n';
        return 1;
    }
}
