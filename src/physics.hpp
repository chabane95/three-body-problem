#ifndef THREE_BODY_PHYSICS_HPP
#define THREE_BODY_PHYSICS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace three_body {

// A host without C++ exceptions can supply a non-returning error callback.
template<class Error>
[[noreturn]] inline void fail(const char* message) {
#ifdef THREE_BODY_ERROR_HANDLER
    THREE_BODY_ERROR_HANDLER(message);
#else
    throw Error(message);
#endif
}

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

using Vec2 = Vec3; // Existing planar callers can keep two-component initializers.
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(double s, Vec3 a) { return {s * a.x, s * a.y, s * a.z}; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline double norm(Vec3 a) { return std::hypot(std::hypot(a.x, a.y), a.z); }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

using Vectors = std::array<Vec3, 3>;
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
        s.velocities[i] = omega * Vec3{-s.positions[i].y, s.positions[i].x, 0};
    }
    return {s, 2.0 * std::acos(-1.0) / omega};
}

inline void validate(const State& s) {
    if (!(s.G > 0.0 && std::isfinite(s.G))) {
        fail<std::invalid_argument>("G must be positive and finite");
    }
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        if (!(s.masses[i] > 0.0 && std::isfinite(s.masses[i])) ||
            !finite(s.positions[i]) || !finite(s.velocities[i])) {
            fail<std::invalid_argument>("Expected finite initial state and positive masses");
        }
    }
}

inline double separation(Vec3 displacement) {
    const double r = norm(displacement);
    if (r == 0.0) {
        fail<std::domain_error>("Coincident bodies: Newtonian gravity is singular");
    }
    if (!std::isfinite(r)) {
        fail<std::overflow_error>("Non-finite separation during integration");
    }
    return r;
}

inline Vectors accelerations(const State& s) {
    Vectors forces{};
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        for (std::size_t j = i + 1; j < s.masses.size(); ++j) {
            const Vec3 dr = s.positions[j] - s.positions[i];
            const double r = separation(dr);
            const double magnitude = s.G * (s.masses[i] / r) * (s.masses[j] / r);
            const Vec3 force = magnitude * (dr / r);
            forces[i] = forces[i] + force;
            forces[j] = forces[j] - force;
        }
    }
    Vectors acceleration;
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        acceleration[i] = forces[i] / s.masses[i];
        if (!finite(acceleration[i])) {
            fail<std::overflow_error>("Non-finite acceleration during integration");
        }
    }
    return acceleration;
}

struct Diagnostics {
    double energy = 0.0;
    Vec3 momentum{};
    double angular_momentum = 0.0; // z component for the original planar export.
    Vec3 angular_momentum_vector{};
    Vec3 center_of_mass{};
    Scalars distances{}; // (1,2), (2,3), (3,1)
};

inline Diagnostics diagnostics(const State& s) {
    Diagnostics d;
    double total_mass = 0.0;
    for (std::size_t i = 0; i < s.masses.size(); ++i) {
        const std::size_t j = (i + 1) % s.masses.size();
        const double m = s.masses[i];
        const Vec3 p = m * s.velocities[i];
        d.distances[i] = separation(s.positions[j] - s.positions[i]);
        d.energy += 0.5 * dot(p, s.velocities[i]) - s.G * m * s.masses[j] / d.distances[i];
        d.momentum = d.momentum + p;
        d.angular_momentum_vector = d.angular_momentum_vector + cross(s.positions[i], p);
        d.center_of_mass = d.center_of_mass + m * s.positions[i];
        total_mass += m;
    }
    d.center_of_mass = d.center_of_mass / total_mass;
    d.angular_momentum = d.angular_momentum_vector.z;
    if (!std::isfinite(d.energy) || !finite(d.momentum) || !finite(d.angular_momentum_vector)
        || !std::isfinite(total_mass) || !finite(d.center_of_mass)) {
        fail<std::overflow_error>("Non-finite diagnostic");
    }
    return d;
}

// Fixed-step velocity Verlet. Carry the endpoint acceleration into the next step.
// The callback observes full-step positions and velocities, including both endpoints.
template<class Observe>
void simulate(State s, double duration, std::size_t steps, std::size_t sample_every, Observe observe) {
    validate(s);
    if (!std::isfinite(duration) || duration <= 0.0 || steps == 0 || sample_every == 0) {
        fail<std::invalid_argument>("A finite positive duration, step count and sample stride are required");
    }
    const double h = duration / static_cast<double>(steps);
    if (h == 0.0) {
        fail<std::underflow_error>("Step size underflow");
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
                fail<std::overflow_error>("Non-finite velocity during integration");
            }
        }
        const std::size_t completed = k + 1;
        if (completed == steps || completed % sample_every == 0) {
            observe(completed == steps ? duration : completed * h, s);
        }
    }
}

struct AdaptiveOptions {
    double rtol = 1e-8;
    double atol_position = 1e-10;
    double atol_velocity = 1e-10;
    double min_distance = 0.0;
    double min_step = 0.0;
    std::size_t max_steps = 250000; // Accepted and rejected trials combined.
};

struct AdaptiveStats {
    std::size_t accepted_steps = 0;
    std::size_t rejected_steps = 0;
    double min_step = std::numeric_limits<double>::infinity();
    double max_step = 0.0;
    double min_distance = std::numeric_limits<double>::infinity(); // Endpoints and resolved periapses.
    double max_error_ratio = 0.0; // Accepted steps only.
};

namespace detail {

struct Encounter {
    double step_bound = std::numeric_limits<double>::infinity();
    double distance = std::numeric_limits<double>::infinity();
};

inline Encounter encounter(const State& s) {
    Encounter result;
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t j = (i + 1) % 3;
        const double r = separation(s.positions[j] - s.positions[i]);
        const double relative_speed = norm(s.velocities[j] - s.velocities[i]);
        const double orbital_speed = std::sqrt(s.G * ((s.masses[i] + s.masses[j]) / r));
        // Resolve both the gravitational timescale and a fast, almost ballistic encounter.
        result.step_bound = std::min(result.step_bound, 0.2 * (r / std::max(orbital_speed, relative_speed)));
        result.distance = std::min(result.distance, r);
    }
    return result;
}

struct Derivative {
    Vectors position;
    Vectors velocity;
};

struct Trial {
    State endpoint;
    double error_ratio;
    Vectors quartic; // Fourth coefficients of the dense interpolants for the three relative positions.
};

inline double scaled_error(Vec3 error, Vec3 initial, Vec3 final, double absolute, double relative) {
    const double scale = absolute + relative * std::max(norm(initial), norm(final));
    if (!finite(error) || !std::isfinite(scale)) fail<std::overflow_error>("Non-finite local error estimate");
    return norm(error) / scale;
}

// Dormand & Prince, J. Comput. Appl. Math. 6 (1980), RK5(4)7M, table 2.
inline Trial dopri_step(const State& s, double h, const AdaptiveOptions& options) {
    static constexpr double a[7][7] = {
        {},
        {1./5},
        {3./40, 9./40},
        {44./45, -56./15, 32./9},
        {19372./6561, -25360./2187, 64448./6561, -212./729},
        {9017./3168, -355./33, 46732./5247, 49./176, -5103./18656},
        {35./384, 0, 500./1113, 125./192, -2187./6784, 11./84}
    };
    static constexpr double fourth[7] = {
        5179./57600, 0, 7571./16695, 393./640, -92097./339200, 187./2100, 1./40
    };
    // Shampine's quartic continuous extension, Math. Comp. 46 (1986), 135–150.
    static constexpr double dense_fourth[7] = {
        -12715105075./11282082432., 0, 87487479700./32700410799., -10690763975./1880347072.,
        701980252875./199316789632., -1453857185./822651844., 69997945./29380423.
    };
    std::array<Derivative, 7> k;
    State stage = s;
    for (std::size_t n = 0; n < k.size(); ++n) {
        for (std::size_t i = 0; i < 3; ++i) {
            Vec3 dq{}, dv{};
            for (std::size_t j = 0; j < n; ++j) {
                dq = dq + a[n][j] * k[j].position[i];
                dv = dv + a[n][j] * k[j].velocity[i];
            }
            stage.positions[i] = s.positions[i] + h * dq;
            stage.velocities[i] = s.velocities[i] + h * dv;
            if (!finite(stage.velocities[i])) fail<std::overflow_error>("Non-finite stage velocity");
        }
        k[n] = {stage.velocities, accelerations(stage)};
    }
    // Stage 7 is the fifth-order solution. Estimate the local error in each pair
    // directly from derivative differences, avoiding subtraction of two large coordinates.
    double error_ratio = 0.0;
    Vectors quartic{};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t j = (i + 1) % 3;
        Vec3 eq{}, ev{};
        for (std::size_t n = 0; n < k.size(); ++n) {
            const double weight = a[6][n] - fourth[n];
            const Vec3 relative_velocity = k[n].position[i] - k[n].position[j];
            eq = eq + weight * relative_velocity;
            ev = ev + weight * (k[n].velocity[i] - k[n].velocity[j]);
            quartic[i] = quartic[i] + dense_fourth[n] * relative_velocity;
        }
        quartic[i] = h * quartic[i];
        error_ratio = std::max({error_ratio,
            scaled_error(h * eq, s.positions[i] - s.positions[j], stage.positions[i] - stage.positions[j],
                         options.atol_position, options.rtol),
            scaled_error(h * ev, s.velocities[i] - s.velocities[j], stage.velocities[i] - stage.velocities[j],
                         options.atol_velocity, options.rtol)});
    }
    return {stage, error_ratio, quartic};
}

// A grazing encounter can cross the cutoff and leave it again between two good
// endpoints. On each transition from approaching to receding, locate r·v = 0
// by bisection on the step's continuous interpolant.
inline double passage_distance(const State& initial, const Trial& trial, double h, double endpoint_distance) {
    double closest = endpoint_distance;
    const State& final = trial.endpoint;
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t j = (i + 1) % 3;
        const Vec3 start = initial.positions[i] - initial.positions[j];
        const Vec3 end = final.positions[i] - final.positions[j];
        const Vec3 left = h * (initial.velocities[i] - initial.velocities[j]);
        const Vec3 right = h * (final.velocities[i] - final.velocities[j]);
        if (dot(start, left) >= 0.0 || dot(end, right) <= 0.0) continue;
        // The positions, two endpoint slopes and fourth coefficient fix the polynomial.
        const Vec3 delta = end - start;
        const Vec3 d = trial.quartic[i];
        const Vec3 b = 3.0 * delta - 2.0 * left - right + d;
        const Vec3 c = -2.0 * delta + left + right - 2.0 * d;
        double low = 0.0, high = 1.0;
        Vec3 r;
        for (int n = 0; n < 40; ++n) {
            const double u = 0.5 * (low + high);
            r = start + u * (left + u * (b + u * (c + u * d)));
            const Vec3 slope = left + u * (2.0 * b + u * (3.0 * c + (4.0 * u) * d));
            if (dot(r, slope) < 0.0) low = u;
            else high = u;
        }
        closest = std::min(closest, norm(r));
    }
    return closest;
}

inline void check_settings(double duration, std::size_t intervals, const AdaptiveOptions& options) {
    for (double value : {duration, options.rtol, options.atol_position, options.atol_velocity}) {
        if (!(value > 0.0 && std::isfinite(value))) {
            fail<std::invalid_argument>("Duration and error tolerances must be positive and finite");
        }
    }
    for (double value : {options.min_distance, options.min_step}) {
        if (!(value >= 0.0 && std::isfinite(value))) {
            fail<std::invalid_argument>("Distance and step cutoffs must be nonnegative and finite");
        }
    }
    if (intervals == 0 || options.max_steps == 0 || intervals == std::numeric_limits<std::size_t>::max()) {
        fail<std::invalid_argument>("A positive output count and trial budget are required");
    }
}

inline void check_distance(double distance, double cutoff) {
    if (distance <= cutoff) fail<std::domain_error>("Minimum separation reached during a close encounter");
}

} // namespace detail

// Observe the initial state and intervals evenly spaced states in the user's inertial frame.
// Internal integration and pairwise error scales are independent of a common velocity boost.
template<class Observe>
AdaptiveStats simulate_adaptive(State s, double duration, std::size_t intervals,
                                const AdaptiveOptions& options, Observe observe) {
    validate(s);
    detail::check_settings(duration, intervals, options);
    AdaptiveStats stats;
    auto geometry = detail::encounter(s);
    detail::check_distance(geometry.distance, options.min_distance);
    stats.min_distance = geometry.distance;
    observe(0.0, s);

    // A uniformly moving origin reduces loss of precision from a large bulk velocity.
    const Vec3 origin = s.positions[0];
    const Vec3 boost = s.velocities[0];
    for (std::size_t i = 0; i < 3; ++i) {
        s.positions[i] = s.positions[i] - origin;
        s.velocities[i] = s.velocities[i] - boost;
    }
    double t = 0.0, next_h = duration / static_cast<double>(intervals);
    for (std::size_t sample = 1; sample <= intervals; ++sample) {
        const double target = duration * (static_cast<double>(sample) / static_cast<double>(intervals));
        if (target <= t) fail<std::underflow_error>("Output times cannot be distinguished at this precision");
        while (t < target) {
            if (stats.accepted_steps + stats.rejected_steps >= options.max_steps) {
                fail<std::runtime_error>("Integration step budget exhausted");
            }
            const double allowed = std::min(next_h, geometry.step_bound);
            const double remaining = target - t;
            const double h = std::min(allowed, remaining);
            // A final short step to an output time is allowed when the controller itself
            // could have used a longer step. Refused trials are retried from the last accepted state.
            if (allowed < options.min_step || !(t + h > t)) {
                fail<std::underflow_error>("Required step is below the minimum or floating-point resolution");
            }
            auto trial = detail::dopri_step(s, h, options);
            const double factor = trial.error_ratio == 0.0 ? 5.0 :
                                  std::clamp(0.9 * std::pow(trial.error_ratio, -0.2), 0.2, 5.0);
            next_h = h * factor;
            if (trial.error_ratio > 1.0) {
                ++stats.rejected_steps;
                continue;
            }
            geometry = detail::encounter(trial.endpoint);
            const double closest = detail::passage_distance(s, trial, h, geometry.distance);
            detail::check_distance(closest, options.min_distance);
            s = trial.endpoint;
            t = (h == remaining) ? target : t + h;
            ++stats.accepted_steps;
            stats.min_step = std::min(stats.min_step, h);
            stats.max_step = std::max(stats.max_step, h);
            stats.min_distance = std::min(stats.min_distance, closest);
            stats.max_error_ratio = std::max(stats.max_error_ratio, trial.error_ratio);
            if (h == remaining && h < allowed && trial.error_ratio < 0.5) {
                // Sampling may leave an arbitrarily small remainder at this boundary.
                next_h = std::max(next_h, allowed);
            }
        }
        State output = s;
        for (std::size_t i = 0; i < 3; ++i) {
            output.positions[i] = origin + (s.positions[i] + target * boost);
            output.velocities[i] = boost + s.velocities[i];
            if (!finite(output.positions[i]) || !finite(output.velocities[i])) {
                fail<std::overflow_error>("Output state exceeds floating-point range");
            }
        }
        observe(target, output);
    }
    return stats;
}

} // namespace three_body

#endif
