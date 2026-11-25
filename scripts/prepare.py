"""Generate the example data and measure convergence against an exact solution."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def integrate(solver, scenario, steps, stride):
    run = subprocess.run(
        [str(solver), "--scenario", scenario, "--periods", "1",
         "--steps-per-period", str(steps), "--sample-every", str(stride)],
        stdout=subprocess.PIPE, check=True, encoding="utf-8",
    )
    return run.stdout


def write_data(path, content):
    with tempfile.TemporaryDirectory(dir=path.parent) as staging:
        temporary = Path(staging) / path.name
        temporary.write_text(content, encoding="utf-8", newline="\n")
        temporary.replace(path)


def energy_error(frames):
    initial = frames[0]["energy"]
    return max(abs(f["energy"] - initial) / abs(initial) for f in frames)


def position_error_lagrange(frames):
    # The reference uses analytical constants independent of the numerical output.
    radius = 1.0 / math.sqrt(3.0)
    omega = math.sqrt(3.0)
    largest = 0.0
    for frame in frames:
        for body, position in enumerate(frame["positions"]):
            angle = omega * frame["t"] + 2.0 * math.pi * body / 3.0
            exact = (radius * math.cos(angle), radius * math.sin(angle))
            largest = max(largest, math.dist(position, exact))
    return largest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    binary = "three-body.exe" if os.name == "nt" else "three-body"
    parser.add_argument("--solver", type=Path, default=ROOT / "build" / binary,
                        help="Path to the compiled C++ solver")
    args = parser.parse_args()
    solver = args.solver.resolve()
    if not solver.is_file():
        parser.error(f"Solver fehlt: {solver}. Bitte zuerst den C++-Kern bauen.")
    destination = ROOT / "data"
    destination.mkdir(exist_ok=True)
    for scenario in ("figure-eight", "lagrange"):
        trajectory = integrate(solver, scenario, 7200, 10)
        frames = json.loads(trajectory)["frames"]
        write_data(destination / f"{scenario}.json", trajectory)
        closure = max(map(math.dist, frames[0]["positions"], frames[-1]["positions"]))
        print(f"{scenario}: {len(frames)} Zeitpunkte, "
              f"max |dE/E0| = {energy_error(frames):.6e}, "
              f"max |r(T)-r(0)| = {closure:.6e}")

    convergence = []
    for steps in (90, 180, 360, 720, 1440, 2880):
        frames = json.loads(integrate(solver, "lagrange", steps, 1))["frames"]
        convergence.append({
            "stepsPerPeriod": steps,
            "positionError": position_error_lagrange(frames),
            "energyError": energy_error(frames),
        })
    write_data(destination / "convergence.json", json.dumps(convergence, indent=2) + "\n")
    order = math.log2(convergence[-2]["positionError"] / convergence[-1]["positionError"])
    print(f"Lagrange: beobachtete Positionsordnung bei letzter Halbierung p = {order:.6f}")


if __name__ == "__main__":
    main()
