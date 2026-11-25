#ifndef THREE_BODY_PHYSICS_HPP
#define THREE_BODY_PHYSICS_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace three_body {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(double s, Vec2 a) { return {s * a.x, s * a.y}; }
inline Vec2 operator/(Vec2 a, double s) { return {a.x / s, a.y / s}; }
inline double norm(Vec2 a) { return std::hypot(a.x, a.y); }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline bool finite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.y); }

using Vectors = std::array<Vec2, 3>;
using Scalars = std::array<double, 3>;

struct State {
    Vectors positions{};
    Vectors velocities{};
    Scalars masses{1.0, 1.0, 1.0};
    double G = 1.0;
};

struct Scenario {
    State initial;
    double period;
};

inline Scenario figure_eight() {
    // Simó's initial data, Fig. 1 in Chenciner & Montgomery (2000).
    State s;
    s.positions[0] = {0.97000436, -0.24308753};
    s.positions[1] = -1.0 * s.positions[0];
    s.velocities[2] = {-0.93240737, -0.86473146};
    s.velocities[0] = s.velocities[1] = -0.5 * s.velocities[2];
    return {s, 6.32591398};
}

// Exact circular equilateral solution for unit side, masses and G.
inline Vectors lagrange_positions(double t) {
    const double pi = std::acos(-1.0);
    const double omega = std::sqrt(3.0);
    Vectors positions;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const double angle = omega * t + 2.0 * pi * i / 3.0;
        positions[i] = {std::cos(angle) / omega, std::sin(angle) / omega};
    }
    return positions;
}

inline Scenario lagrange() {
    State s;
    s.positions = lagrange_positions(0.0);
    const double omega = std::sqrt(3.0);
    for (std::size_t i = 0; i < s.positions.size(); ++i) {
        s.velocities[i] = omega * Vec2{-s.positions[i].y, s.positions[i].x};
    }
    return {s, 2.0 * std::acos(-1.0) / omega};
}

inline void validate(const State& s) {
    if (!(s.G > 0.0 && std::isfinite(s.G))) {
        throw std::invalid_argument("G must be positive and finite");
    }
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        if (!(s.masses[i] > 0.0 && std::isfinite(s.masses[i])) ||
            !finite(s.positions[i]) || !finite(s.velocities[i])) {
            throw std::invalid_argument("Expected finite initial state and positive masses");
        }
    }
}

inline double separation(Vec2 displacement) {
    const double r = norm(displacement);
    if (r == 0.0) {
        throw std::domain_error("Coincident bodies: Newtonian gravity is singular");
    }
    if (!std::isfinite(r)) {
        throw std::overflow_error("Non-finite separation during integration");
    }
    return r;
}

inline Vectors accelerations(const State& s) {
    Vectors forces{};
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        for (std::size_t j = i + 1; j < s.masses.size(); ++j) {
            const Vec2 dr = s.positions[j] - s.positions[i];
            const double r = separation(dr);
            const double magnitude = s.G * (s.masses[i] / r) * (s.masses[j] / r);
            const Vec2 force = magnitude * (dr / r);
            forces[i] = forces[i] + force;
            forces[j] = forces[j] - force;
        }
    }
    Vectors acceleration;
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        acceleration[i] = forces[i] / s.masses[i];
        if (!finite(acceleration[i])) {
            throw std::overflow_error("Non-finite acceleration during integration");
        }
    }
    return acceleration;
}

struct Diagnostics {
    double energy = 0.0;
    Vec2 momentum{};
    double angular_momentum = 0.0;
    Vec2 center_of_mass{};
    Scalars distances{}; // (1,2), (2,3), (3,1)
};

inline Diagnostics diagnostics(const State& s) {
    Diagnostics d;
    double total_mass = 0.0;
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        const std::size_t j = (i + 1) % s.masses.size();
        const double m = s.masses[i];
        const Vec2 p = m * s.velocities[i];
        d.distances[i] = separation(s.positions[j] - s.positions[i]);
        d.energy += 0.5 * dot(p, s.velocities[i]) - s.G * m * s.masses[j] / d.distances[i];
        d.momentum = d.momentum + p;
        d.angular_momentum += cross(s.positions[i], p);
        d.center_of_mass = d.center_of_mass + m * s.positions[i];
        total_mass += m;
    }
    d.center_of_mass = d.center_of_mass / total_mass;
    if (!std::isfinite(d.energy) || !finite(d.momentum) || !std::isfinite(d.angular_momentum)
        || !finite(d.center_of_mass)) {
        throw std::overflow_error("Non-finite diagnostic");
    }
    return d;
}

// Fixed-step velocity Verlet. Carry the endpoint acceleration into the next step.
// The callback observes full-step positions and velocities, including both endpoints.
template<class Observe>
void simulate(State s, double duration, std::size_t steps, std::size_t sample_every, Observe observe) {
    validate(s);
    if (!std::isfinite(duration) || duration <= 0.0 || steps == 0 || sample_every == 0) {
        throw std::invalid_argument("A finite positive duration, step count and sample stride are required");
    }
    const double h = duration / static_cast<double>(steps);
    if (h == 0.0) {
        throw std::underflow_error("Step size underflow");
    }
    Vectors acceleration = accelerations(s);
    observe(0.0, s);
    for (std::size_t k = 0; k < steps; ++k) {
        for (std::size_t i = 0; i < s.masses.size(); ++i) {
            s.velocities[i] = s.velocities[i] + (0.5 * h) * acceleration[i];
            s.positions[i] = s.positions[i] + h * s.velocities[i];
        }
        acceleration = accelerations(s);
        for (std::size_t i = 0; i < s.masses.size(); ++i) {
            s.velocities[i] = s.velocities[i] + (0.5 * h) * acceleration[i];
            if (!finite(s.velocities[i])) {
                throw std::overflow_error("Non-finite velocity during integration");
            }
        }
        const std::size_t completed = k + 1;
        if (completed == steps || completed % sample_every == 0) {
            observe(completed == steps ? duration : completed * h, s);
        }
    }
}

} // namespace three_body

#endif
