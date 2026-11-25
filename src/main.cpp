#include "physics.hpp"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

using namespace three_body;

void usage() {
    std::cout <<
        "Planar reference trajectories (fixed velocity Verlet):\n"
        "  three-body [--scenario figure-eight|lagrange] [--periods N]\n"
        "             [--steps-per-period N] [--sample-every N]\n"
        "Custom 3D initial values (adaptive Dormand-Prince):\n"
        "  three-body --body M X Y Z VX VY VZ --body ... --body ... --duration T\n"
        "             [--intervals N] [--gravity G] [--rtol R]\n"
        "             [--atol-position A] [--atol-velocity A] [--min-distance R]\n"
        "Standard output is JSON. Each custom run returns N+1 equally spaced frames.\n";
}

std::size_t positive_integer(std::string_view text) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
        throw std::invalid_argument("Expected a positive integer: " + std::string(text));
    return value;
}

double number(const std::string& text) {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Expected a finite number: " + text);
    return value;
}

struct Settings {
    State state;
    std::size_t bodies = 0;
    double duration = 0.0;
    std::size_t intervals = 720;
    AdaptiveOptions adaptive;
    std::string preset = "figure-eight";
    double periods = 1.0;
    std::size_t steps_per_period = 7200;
    std::size_t sample_every = 20;
};

Settings parse(int argc, char** argv) {
    Settings settings;
    bool reference_flags = false, custom_flags = false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const auto next = [&]() -> std::string {
            if (++i >= argc) throw std::invalid_argument("Missing value for " + key);
            return argv[i];
        };
        if (key == "--body") {
            custom_flags = true;
            const auto b = settings.bodies++;
            if (b >= 3) throw std::invalid_argument("Exactly three --body arguments are required");
            settings.state.masses[b] = number(next());
            settings.state.positions[b] = {number(next()), number(next()), number(next())};
            settings.state.velocities[b] = {number(next()), number(next()), number(next())};
        } else if (key == "--scenario" || key == "--periods" || key == "--steps-per-period" || key == "--sample-every") {
            reference_flags = true;
            if (key == "--scenario") settings.preset = next();
            else if (key == "--periods") settings.periods = number(next());
            else if (key == "--steps-per-period") settings.steps_per_period = positive_integer(next());
            else settings.sample_every = positive_integer(next());
        } else {
            custom_flags = true;
            if (key == "--duration") settings.duration = number(next());
            else if (key == "--intervals") settings.intervals = positive_integer(next());
            else if (key == "--gravity") settings.state.G = number(next());
            else if (key == "--rtol") settings.adaptive.rtol = number(next());
            else if (key == "--atol-position") settings.adaptive.atol_position = number(next());
            else if (key == "--atol-velocity") settings.adaptive.atol_velocity = number(next());
            else if (key == "--min-distance") settings.adaptive.min_distance = number(next());
            else throw std::invalid_argument("Unknown option " + key);
        }
    }
    if (reference_flags && custom_flags) throw std::invalid_argument("Choose a reference run or a custom 3D run");
    if (custom_flags && (settings.bodies != 3 || settings.duration <= 0.0))
        throw std::invalid_argument("Custom runs require three --body arguments and a positive --duration");
    if (settings.preset != "figure-eight" && settings.preset != "lagrange")
        throw std::invalid_argument("Unknown preset " + settings.preset);
    if (settings.periods <= 0.0) throw std::invalid_argument("--periods must be positive");
    return settings;
}

void json_value(double value, bool) { std::cout << value; }

void json_value(Vec3 value, bool spatial) {
    std::cout << '[' << value.x << ',' << value.y;
    if (spatial) std::cout << ',' << value.z;
    std::cout << ']';
}

template<class T, std::size_t N>
void json_value(const std::array<T, N>& values, bool spatial) {
    std::cout << '[';
    for (std::size_t i = 0; i < N; ++i) {
        if (i) std::cout << ',';
        json_value(values[i], spatial);
    }
    std::cout << ']';
}

void write_frame(double t, const State& s, const Diagnostics& d, bool spatial) {
    std::cout << "{\"t\":" << t << ",\"positions\":";
    json_value(s.positions, spatial);
    std::cout << ",\"velocities\":"; json_value(s.velocities, spatial);
    std::cout << ",\"energy\":" << d.energy << ",\"momentum\":"; json_value(d.momentum, spatial);
    std::cout << ",\"angularMomentum\":" << d.angular_momentum;
    if (spatial) {
        std::cout << ",\"angularMomentumVector\":";
        json_value(d.angular_momentum_vector, true);
    }
    std::cout << ",\"centerOfMass\":"; json_value(d.center_of_mass, spatial);
    std::cout << ",\"distances\":"; json_value(d.distances, spatial);
    std::cout << '}';
}

void write_run(const Settings& settings) {
    const bool spatial = settings.bodies == 3;
    const auto begin_frames = [](const State& s) {
        std::cout << ",\"G\":" << s.G << ",\"masses\":";
        json_value(s.masses, true);
        std::cout << ",\"frames\":[\n";
    };
    auto observe = [spatial, first = true](double t, const State& s) mutable {
        const auto d = diagnostics(s);
        if (!first) std::cout << ",\n";
        write_frame(t, s, d, spatial);
        first = false;
    };
    if (spatial) {
        validate(settings.state);
        std::cout << "{\"scenario\":\"custom\",\"integrator\":\"DOPRI5(4)\",\"duration\":" << settings.duration
                  << ",\"intervals\":" << settings.intervals << ",\"rtol\":" << settings.adaptive.rtol
                  << ",\"atolPosition\":" << settings.adaptive.atol_position
                  << ",\"atolVelocity\":" << settings.adaptive.atol_velocity
                  << ",\"minDistance\":" << settings.adaptive.min_distance;
        begin_frames(settings.state);
        AdaptiveStats stats;
        try {
            stats = simulate_adaptive(settings.state, settings.duration, settings.intervals, settings.adaptive, observe);
        } catch (const std::exception&) {
            std::cout << "\n],\"completed\":false}\n";
            throw;
        }
        std::cout << "\n],\"completed\":true,\"stats\":{\"accepted\":" << stats.accepted_steps
                  << ",\"rejected\":" << stats.rejected_steps << ",\"minStep\":" << stats.min_step
                  << ",\"maxStep\":" << stats.max_step << ",\"minDistance\":" << stats.min_distance
                  << ",\"maxErrorRatio\":" << stats.max_error_ratio << "}}\n";
        return;
    }
    const auto scenario = settings.preset == "lagrange" ? lagrange() : figure_eight();
    const double duration = settings.periods * scenario.period;
    const double count = std::ceil(settings.periods * static_cast<double>(settings.steps_per_period));
    if (!std::isfinite(duration) || count < 1.0 || count > 9007199254740991.0 ||
        count >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
        throw std::invalid_argument("Requested integration interval is out of range");
    const auto steps = static_cast<std::size_t>(count);
    std::cout << "{\"scenario\":\"" << settings.preset << "\",\"period\":" << scenario.period
              << ",\"stepsPerPeriod\":" << settings.steps_per_period << ",\"steps\":" << steps
              << ",\"stepSize\":" << duration / steps << ",\"sampleEvery\":" << settings.sample_every;
    begin_frames(scenario.initial);
    simulate(scenario.initial, duration, steps, settings.sample_every, observe);
    std::cout << "\n]}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
            usage();
            return 0;
        }
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        write_run(parse(argc, argv));
        std::cout.flush();
        return std::cout ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "three-body: " << e.what() << '\n';
        return 1;
    }
}
