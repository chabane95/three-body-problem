extern "C" [[noreturn]] __attribute__((import_module("env"), import_name("fail")))
void abort_simulation(const char* message);

#define THREE_BODY_ERROR_HANDLER abort_simulation
#include "physics.hpp"
#undef THREE_BODY_ERROR_HANDLER

#include <array>

extern "C" __attribute__((import_module("env"), import_name("sample")))
void publish_sample(double time, const double* values);

namespace {
using namespace three_body;

// Seven doubles per body: mass, x, y, z, vx, vy, vz.
std::array<double, 21> input{};
std::array<double, 6> statistics{};

void observe(double t, const State& state) {
    const auto diagnostics = three_body::diagnostics(state);
    std::array<double, 31> sample{};
    auto out = sample.begin();
    auto add_vec = [&](Vec3 value) {
        *out++ = value.x;
        *out++ = value.y;
        *out++ = value.z;
    };
    for (const auto& position : state.positions) add_vec(position);
    for (const auto& velocity : state.velocities) add_vec(velocity);
    *out++ = diagnostics.energy;
    add_vec(diagnostics.momentum);
    add_vec(diagnostics.angular_momentum_vector);
    add_vec(diagnostics.center_of_mass);
    for (const auto distance : diagnostics.distances) *out++ = distance;
    publish_sample(t, sample.data());
}
}  // namespace

extern "C" {

double* input_buffer() { return input.data(); }
const double* stats_buffer() { return statistics.data(); }

void integrate(double G, double duration, unsigned intervals,
               double rtol, double atol_position, double atol_velocity,
               double min_distance, double min_step, unsigned max_steps) {
    three_body::State state;
    state.G = G;
    for (std::size_t i = 0; i < state.masses.size(); ++i) {
        const double* b = input.data() + 7 * i;
        state.masses[i] = b[0];
        state.positions[i] = {b[1], b[2], b[3]};
        state.velocities[i] = {b[4], b[5], b[6]};
    }
    three_body::AdaptiveOptions options;
    options.rtol = rtol;
    options.atol_position = atol_position;
    options.atol_velocity = atol_velocity;
    options.min_distance = min_distance;
    options.min_step = min_step;
    options.max_steps = max_steps;
    const auto result = three_body::simulate_adaptive(state, duration, intervals, options, observe);
    statistics = {static_cast<double>(result.accepted_steps), static_cast<double>(result.rejected_steps),
                  result.min_step, result.max_step, result.min_distance, result.max_error_ratio};
}
}  // extern "C"
