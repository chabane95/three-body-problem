"""Build the browser reactor with the WASI SDK (tested with version 33)."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=os.environ.get("WASI_SDK_PATH"),
                        help="WASI SDK directory, or set WASI_SDK_PATH")
    args = parser.parse_args()
    if not args.sdk:
        parser.error("Specify --sdk PATH or set WASI_SDK_PATH")
    sdk = Path(args.sdk).resolve()
    compiler = sdk / "bin" / ("clang++.exe" if os.name == "nt" else "clang++")
    if not compiler.is_file():
        parser.error(f"Compiler not found: {compiler}")
    target = ROOT / "src" / "solver.wasm"
    with tempfile.TemporaryDirectory(dir=target.parent) as staging:
        wasm = Path(staging) / target.name
        subprocess.run([
            str(compiler), "--target=wasm32-wasip1", "-std=c++17", "-O2",
            "-fno-exceptions",
            "-mexec-model=reactor", "-Wl,--strip-all",
            "-Wl,--export=input_buffer", "-Wl,--export=stats_buffer", "-Wl,--export=integrate",
            str(ROOT / "src" / "wasm.cpp"), "-o", str(wasm),
        ], check=True)
        wasm.replace(target)
    print(f"{target.name}: {target.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
