"""Package the 3D solver, worker, viewer and planar reference orbits for offline use."""

import base64
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
NAME = "Dreikoerperproblem"


def replace_once(html, original, replacement):
    if html.count(original) != 1:
        raise ValueError(f"Einbindung in index.html fehlt oder ist mehrdeutig: {original}")
    return html.replace(original, replacement)


def main():
    page = (ROOT / "index.html").read_text(encoding="utf-8")
    css = (ROOT / "src/style.css").read_text(encoding="utf-8")
    js = (ROOT / "src/viewer.mjs").read_text(encoding="utf-8")
    page = replace_once(page, '<link rel="stylesheet" href="src/style.css">', f"<style>\n{css}\n</style>")
    page = replace_once(page, '<script type="module" src="src/viewer.mjs"></script>', "")
    page = replace_once(page, 'href="README.md">Dokumentation',
                        'href="https://github.com/chabane95/three-body-problem#readme">Dokumentation auf GitHub')
    scripts = []
    preset = (ROOT / "data/flyby-preset.json").read_text(encoding="utf-8")
    scripts.append(f'<script type="application/json" id="flyby-preset">{preset}</script>')
    for name in ("figure-eight", "lagrange"):
        data = (ROOT / "data" / f"{name}.json").read_text(encoding="utf-8")
        scripts.append(f'<script type="application/json" id="data-{name}">{data}</script>')
    worker = (ROOT / "src/solver-worker.js").read_text(encoding="utf-8")
    wasm = base64.b64encode((ROOT / "src/solver.wasm").read_bytes()).decode("ascii")
    scripts.append(f'<script type="text/plain" id="solver-worker">{worker}</script>')
    scripts.append(f'<script type="application/octet-stream" id="solver-wasm">{wasm}</script>')
    scripts.append(f'<script type="module">\n{js}\n</script>')
    page = replace_once(page, "</body>", "\n".join(scripts) + "\n</body>")
    html = page.encode("utf-8")
    output = ROOT / "dist"
    output.mkdir(exist_ok=True)
    (output / f"{NAME}.html").write_bytes(html)

    downloads = ROOT / "downloads"
    downloads.mkdir(exist_ok=True)
    # Fixed archive metadata makes the download reproducible from the same sources.
    entry = zipfile.ZipInfo(f"{NAME}.html", date_time=(1980, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.create_system = 3
    entry.external_attr = 0o100644 << 16
    with zipfile.ZipFile(downloads / f"{NAME}.zip", "w") as archive:
        archive.writestr(entry, html)
    print(f"dist/{NAME}.html und downloads/{NAME}.zip erstellt")


if __name__ == "__main__":
    main()
