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
    std::cout << "three-body [--scenario figure-eight|lagrange] [--periods FLOAT]\n"
                 "           [--steps-per-period INT] [--sample-every INT]\n"
                 "Writes a JSON trajectory to stdout. Defaults: figure-eight, 1, 7200, 20.\n";
}

std::size_t positive_integer(std::string_view text) {
    std::size_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument("Expected a positive integer, got " + std::string(text));
    }
    return value;
}

double positive_number(const std::string& text) {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument("Expected a positive finite number, got " + text);
    }
    return value;
}

void write_json(double value) {
    std::cout << value;
}

void write_json(Vec2 value) {
    std::cout << '[' << value.x << ',' << value.y << ']';
}

template<class T, std::size_t N>
void write_json(const std::array<T, N>& values) {
    std::cout << '[';
    for (std::size_t i = 0; i < N; ++i) {
        if (i != 0) std::cout << ',';
        write_json(values[i]);
    }
    std::cout << ']';
}

void write_frame(double t, const State& state) {
    const auto d = diagnostics(state);
    std::cout << "{\"t\":" << t << ",\"positions\":";
    write_json(state.positions);
    std::cout << ",\"velocities\":";
    write_json(state.velocities);
    std::cout << ",\"energy\":" << d.energy << ",\"momentum\":";
    write_json(d.momentum);
    std::cout << ",\"angularMomentum\":" << d.angular_momentum << ",\"centerOfMass\":";
    write_json(d.center_of_mass);
    std::cout << ",\"distances\":";
    write_json(d.distances);
    std::cout << '}';
}

} // namespace

int main(int argc, char** argv) {
    using namespace three_body;
    try {
        std::string scenario_name = "figure-eight";
        double periods = 1.0;
        std::size_t steps_per_period = 7200;
        std::size_t sample_every = 20;
        for (int arg = 1; arg < argc; ++arg) {
            const std::string option = argv[arg];
            if (option == "--help" || option == "-h") {
                usage();
                return 0;
            }
            if (arg + 1 == argc) throw std::invalid_argument("Missing value for " + option);
            const std::string value = argv[++arg];
            if (option == "--scenario") scenario_name = value;
            else if (option == "--periods") periods = positive_number(value);
            else if (option == "--steps-per-period") steps_per_period = positive_integer(value);
            else if (option == "--sample-every") sample_every = positive_integer(value);
            else throw std::invalid_argument("Unknown option " + option);
        }
        if (scenario_name != "figure-eight" && scenario_name != "lagrange") {
            throw std::invalid_argument("Expected --scenario figure-eight or lagrange");
        }
        const auto scenario = scenario_name == "lagrange" ? lagrange() : figure_eight();
        const double duration = periods * scenario.period;
        const double count = std::ceil(periods * static_cast<double>(steps_per_period));
        // Beyond 2^53 even consecutive integer step indices need not map to distinct doubles.
        if (!std::isfinite(duration) || count < 1.0 || count > 9007199254740991.0 ||
            count >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            throw std::invalid_argument("Requested integration interval is out of range");
        }
        const auto steps = static_cast<std::size_t>(count);
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        std::cout << "{\"scenario\":\"" << scenario_name << "\",\"period\":" << scenario.period
                  << ",\"stepsPerPeriod\":" << steps_per_period << ",\"steps\":" << steps
                  << ",\"stepSize\":" << duration / steps << ",\"sampleEvery\":" << sample_every
                  << ",\"G\":" << scenario.initial.G << ",\"masses\":";
        write_json(scenario.initial.masses);
        std::cout << ",\"frames\":[\n";
        bool first = true;
        simulate(scenario.initial, duration, steps, sample_every, [&](double t, const State& s) {
            if (!first) std::cout << ",\n";
            write_frame(t, s);
            first = false;
        });
        std::cout << "\n]}\n";
        std::cout.flush();
        return std::cout ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "three-body: " << e.what() << '\n';
        return 1;
    }
}
