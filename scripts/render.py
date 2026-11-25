"""Create the figures directly from the C++ solver output. Requires Pillow."""

import argparse
import bisect
import json
import math
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageColor, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
COLORS = ("#206B78", "#AA5D2B", "#705595")
PAPER, INK, AXIS, GUIDE = "#FFFFFF", "#22272B", "#566168", "#9EAAB0"
SOFT = Image.Resampling.LANCZOS


@lru_cache(maxsize=None)
def font(size):
    for name in ("segoeui.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf",
                 "/System/Library/Fonts/Supplemental/Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def mix(a, b, fraction):
    return tuple(round(x + (y - x) * fraction)
                 for x, y in zip(ImageColor.getrgb(a), ImageColor.getrgb(b)))


class Canvas:
    """Use one set of marks for the antialiased raster and vector figures."""

    def __init__(self, width, height, *, print_figure=False):
        self.width, self.height = width, height
        self.s = 6 if print_figure else 2
        self.image = Image.new("RGB", (width*self.s, height*self.s), PAPER)
        self.draw = ImageDraw.Draw(self.image)
        self.svg = None
        if print_figure:
            self.svg = ET.Element("svg", xmlns="http://www.w3.org/2000/svg",
                                  width=str(width), height=str(height), viewBox=f"0 0 {width} {height}")
            ET.SubElement(self.svg, "rect", width="100%", height="100%", fill=PAPER)

    def copy(self):
        other = Canvas.__new__(Canvas)
        other.width, other.height, other.s = self.width, self.height, self.s
        other.image = self.image.copy()
        other.draw = ImageDraw.Draw(other.image)
        other.svg = None
        return other

    def label(self, x, y, text, size=13, anchor="mm", color=INK, vertical=False):
        """Position labels by horizontal anchor and vertical centre in both formats."""
        s, face = self.s, font(round(self.s*size))
        if vertical:
            box = face.getbbox(text)
            w, h = box[2] - box[0], box[3] - box[1]
            mask = Image.new("L", (w + 4, h + 4))
            ImageDraw.Draw(mask).text((2-box[0], 2-box[1]), text, font=face, fill=255)
            mask = mask.transpose(Image.Transpose.ROTATE_90)
            self.image.paste(color, (round(x*s-mask.width/2), round(y*s-mask.height/2)), mask)
        else:
            self.draw.text((x*s, y*s), text, font=face, fill=color, anchor=anchor)
        if self.svg is not None:
            attrs = {"x":str(x), "y":str(y), "fill":color, "font-size":str(size),
                     "font-family":"Segoe UI, DejaVu Sans, Liberation Sans, sans-serif",
                     "text-anchor":{"l":"start", "m":"middle", "r":"end"}[anchor[0]],
                     "dominant-baseline":"central"}
            if vertical:
                attrs["transform"] = f"rotate(-90 {x} {y})"
            ET.SubElement(self.svg, "text", attrs).text = text

    def path(self, points, color, width=1):
        self.draw.line([(round(x*self.s), round(y*self.s)) for x, y in points],
                       fill=color, width=max(1, round(width*self.s)), joint="curve")
        if self.svg is not None:
            sequence = " ".join(f"{x:.3f},{y:.3f}" for x, y in points)
            ET.SubElement(self.svg, "polyline", {"points":sequence, "stroke":color,
                          "stroke-width":str(width), "fill":"none", "stroke-linejoin":"round"})

    def styled_path(self, points, color, width, pattern=()):
        if not pattern:
            self.path(points, color, width)
            return
        # Dash lengths are in display coordinates, including across sample boundaries.
        i, remaining, segment = 0, pattern[0], [points[0]]
        for start, end in zip(points, points[1:]):
            length = math.dist(start, end)
            while length > 1e-9:
                step = min(remaining, length)
                f = step / length
                tip = tuple(a+(b-a)*f for a, b in zip(start, end))
                if i % 2 == 0:
                    segment.append(tip)
                remaining -= step
                if remaining < 1e-8:
                    if i % 2 == 0:
                        self.path(segment, color, width)
                    i = (i+1) % len(pattern)
                    remaining = pattern[i]
                    segment = [tip]
                start, length = tip, length - step
        if i % 2 == 0 and len(segment) > 1:
            self.path(segment, color, width)

    def dot(self, x, y, r, color):
        s = self.s
        self.draw.ellipse(tuple(round(v*s) for v in (x-r, y-r, x+r, y+r)),
                          fill=color, outline=PAPER, width=max(1, round(.7*s)))
        if self.svg is not None:
            ET.SubElement(self.svg, "circle", {"cx":f"{x:.3f}", "cy":f"{y:.3f}", "r":str(r),
                          "fill":color, "stroke":PAPER, "stroke-width":"0.7"})

    def legend_box(self, left, top, right, bottom):
        self.path([(left,top), (right,top), (right,bottom), (left,bottom), (left,top)], "#D2D6D7", .65)

    def finish(self):
        return self.image.resize((self.width, self.height), SOFT)

    def save_figure(self, dest, stem):
        self.image.resize((self.width*3, self.height*3), SOFT).save(dest/f"{stem}.png", dpi=(400,400))
        ET.ElementTree(self.svg).write(dest/f"{stem}.svg", encoding="utf-8", xml_declaration=True)


def number(x, digits=2):
    if abs(x) < .5 * 10**-digits:
        x = 0
    return f"{x:.{digits}f}".rstrip("0").rstrip(".").replace(".", ",").replace("-", "−")


def power10(p):
    return "10" + str(int(p)).translate(str.maketrans("-0123456789", "⁻⁰¹²³⁴⁵⁶⁷⁸⁹"))


def nearest_phase(run):
    times = [f["t"] for f in run["frames"]]

    def nearest(phase):
        target = run["period"] * phase
        i = min(bisect.bisect_left(times, target), len(times)-1)
        return i-1 if i and abs(times[i-1]-target) < abs(times[i]-target) else i
    return nearest


def axes(c, rect, xlim, ylim, xticks, yticks, xlabel, ylabel, *, quiet=False,
         xfmt=number, yfmt=number, y_label_offset=44, minor_y=()):
    left, top, right, bottom = rect
    def project(p):
        return (left+(p[0]-xlim[0])*(right-left)/(xlim[1]-xlim[0]),
                bottom-(p[1]-ylim[0])*(bottom-top)/(ylim[1]-ylim[0]))

    line, ink = ("#C6CDD0", "#647179") if quiet else ("#202427", "#171A1C")
    if quiet:
        c.path([(left, top), (left, bottom), (right, bottom)], line, .8)
    else:
        c.path([(left, top), (right, top), (right, bottom), (left, bottom), (left, top)], line, 1.05)
    for tick in xticks:
        x, y = project((tick, ylim[0]))
        c.path([(x, y), (x, y-5)], line, .9)
        if not quiet:
            c.path([(x, top), (x, top+4)], line, .9)
        c.label(x, bottom+15, xfmt(tick), 12, color=ink)
    for tick in yticks:
        x, y = project((xlim[0], tick))
        c.path([(x, y), (x+5, y)], line, .9)
        if not quiet:
            c.path([(right, y), (right-4, y)], line, .9)
        c.label(left-8, y, yfmt(tick), 12, "rm", ink)
    for tick in minor_y:
        _, y = project((xlim[0], tick))
        c.path([(left,y), (left+2.5,y)], line, .7)
        c.path([(right,y), (right-2.5,y)], line, .7)
    c.label((left+right)/2, bottom+35, xlabel, 14, color=ink)
    c.label(left-y_label_offset, (top+bottom)/2, ylabel, 14, color=ink, vertical=True)
    return project


def motion_base(run):
    c = Canvas(960, 438)
    project = axes(c, (71, 48, 933, 375), (-1.16,1.16), (-.44,.44), [-1,-.5,0,.5,1],
                   [-.4,-.2,0,.2,.4], "x/L₀", "y/L₀", quiet=True)
    c.path([project(f["positions"][0]) for f in run["frames"]], "#AFBBC0", 1.2)
    for j, color in enumerate(COLORS):
        x = 702 + 80*j
        c.dot(x, 25, 4.2, color)
        c.label(x+10, 25, f"Körper {j+1}", 13, "lm")
    return c, project


def motion_frame(base, project, run, index):
    c, frames, period = base.copy(), run["frames"], run["period"]
    frame = frames[index]
    first_time = frame["t"] - .09*period
    start = index
    while start and frames[start-1]["t"] >= first_time:
        start -= 1
    for j, color in enumerate(COLORS):
        for i in range(start+1, index+1):
            fraction = min(1., (frames[i]["t"]-first_time)/(.09*period))
            shade = mix("#AFBBC0", color, fraction**1.2)
            c.path([project(frames[i-1]["positions"][j]), project(frames[i]["positions"][j])],
                   shade, 1.3+1.5*fraction)
        c.dot(*project(frame["positions"][j]), 8, color)
    phase = f"{frame['t']/period:.3f}".replace(".", ",")
    c.label(71, 25, f"t/T = {phase}", 14, "lm")
    return c.finish()


def motion(run, dest):
    nearest = nearest_phase(run)
    base, project = motion_base(run)
    def render(phase):
        return motion_frame(base, project, run, nearest(phase))
    for phase, stem in ((0, "start"), (1/12, "twelfth"), (.25, "quarter")):
        render(phase).save(dest/f"snapshot-{stem}.png")
    # Keep colours fixed between frames; omit the duplicate endpoint for the loop.
    sample = Image.new("RGB", (base.width, base.height))
    w, h = base.width//2, base.height//2
    for i in range(4):
        sample.paste(render(i*.23).resize((w,h), SOFT), (i%2*w, i//2*h))
    palette = sample.quantize(256, method=Image.Quantize.MEDIANCUT)
    movie = [render(i/100).quantize(palette=palette, dither=Image.Dither.NONE) for i in range(100)]
    movie[0].save(dest/"figure-eight.gif", save_all=True, append_images=movie[1:],
                  duration=60, loop=0, disposal=1, optimize=False)


def trajectories(acht, lagrange, dest):
    c = Canvas(895, 309, print_figure=True)
    # Both panels use equal x and y scales. Their horizontal domains differ.
    height = 218
    for run, left, width, title, xrange, xticks, yticks in (
        (acht, 64, 460, "(a)  Achterbahn", 1.18, [-1,-.5,0,.5,1], [-.4,0,.4]),
        (lagrange, 652, 218, "(b)  Lagrange-Dreieck", .69, [-.5,0,.5], [-.5,0,.5]),
    ):
        yrange = xrange*height/width
        c.label(left, 18, title, 14, "lm")
        project = axes(c, (left,40,left+width,40+height), (-xrange,xrange), (-yrange,yrange),
                       xticks, yticks, "x/L₀", "y/L₀")
        frames, nearest = run["frames"], nearest_phase(run)
        first, last = nearest(1/24), nearest(1/8)
        c.path([project(f["positions"][0]) for f in frames], GUIDE, 1.1)
        if run is lagrange:
            points = [project(p) for p in frames[last]["positions"]]
            c.path(points+[points[0]], "#BBC3C6", .8)
        for j, color in enumerate(COLORS):
            c.path([project(f["positions"][j]) for f in frames[first:last+1]], color, 2.0)
            c.dot(*project(frames[last]["positions"][j]), 4.7, color)
    c.legend_box(176,45,411,67)
    for j, color in enumerate(COLORS):
        x = 187 + j*78
        c.dot(x, 56, 3.6, color)
        c.label(x+9, 56, f"Körper {j+1}", 12, "lm")
    c.save_figure(dest, "trajectories")


def diagnostics(acht, convergence, dest):
    c = Canvas(915, 325, print_figure=True)
    c.label(66, 15, "(a)  Achterbahn", 14, "lm")
    c.label(549, 15, "(b)  Lagrange-Dreieck", 14, "lm")
    frames, period = acht["frames"], acht["period"]
    e0 = frames[0]["energy"]
    errors = [(f["energy"]-e0)/abs(e0) for f in frames]
    power = math.floor(math.log10(max(map(abs, errors))))
    scaled = [v/10**power for v in errors]
    low, high = math.floor(min(scaled)), math.ceil(max(scaled))
    c.label(66, 42, "×" + power10(power), 12, "lm", AXIS)
    p = axes(c, (66,55,420,273), (0,1), (low,high), [0,.25,.5,.75,1], list(range(low,high+1)),
             "t/T", "ΔE / |E₀|", y_label_offset=41)
    c.path([p((0,0)), p((1,0))], "#D2D8DA", .65)
    c.path([p((f["t"]/period,v)) for f, v in zip(frames, scaled)], "#214E6A", 1.65)

    rows = sorted(convergence, key=lambda row: row["stepsPerPeriod"])
    ns = [row["stepsPerPeriod"] for row in rows]
    xs = [math.log2(n) for n in ns]
    ys = [math.log10(row["positionError"]) for row in rows]
    guide = [ys[0]+.45 - 2*math.log10(n/ns[0]) for n in ns]
    lo, hi = math.floor(min(ys+guide)), math.ceil(max(ys+guide))
    minor = [k+math.log10(m) for k in range(lo,hi) for m in (2,5)]
    p = axes(c, (549,55,890,273), (xs[0]-.1,xs[-1]+.1), (lo,hi), xs, list(range(lo,hi+1)),
             "Schritte pro Periode", "Max. Positionsfehler / L₀", xfmt=lambda x:str(round(2**x)),
             yfmt=power10, y_label_offset=56, minor_y=minor)
    measured = "#A83C3B"
    c.styled_path([p((x,y)) for x, y in zip(xs, guide)], AXIS, 1.0, (5,3))
    c.path([p((x,y)) for x, y in zip(xs, ys)], measured, 1.65)
    for x,y in zip(xs,ys):
        c.dot(*p((x,y)), 3.6, measured)
    c.legend_box(709,67,878,90)
    c.path([(718,78),(741,78)], measured, 1.65)
    c.dot(730,78,3.6,measured)
    c.label(746, 78, "Messung", 12, "lm")
    c.styled_path([(808,78),(830,78)], AXIS, 1, (5,3))
    c.label(834,78,"O(h²)",12,"lm")
    c.save_figure(dest, "diagnostics")


def distances(acht, dest):
    c = Canvas(780, 309, print_figure=True)
    frames, period = acht["frames"], acht["period"]
    largest = max(max(f["distances"]) for f in frames)
    top = math.ceil(largest*1.06*4)/4
    ticks = [i/2 for i in range(math.floor(top*2)+1)]
    p = axes(c, (70,16,760,258), (0,1), (0,top), [0,.125,.25,.375,.5,.625,.75,.875,1],
             ticks, "t/T", "Paarabstand / L₀", xfmt=lambda x:number(x,3), y_label_offset=48)
    c.legend_box(276,223,543,245)
    for j, (name, style) in enumerate((("r₁₂", ()), ("r₂₃", (6,3)), ("r₃₁", (2,2.8)))):
        color = COLORS[j]
        c.styled_path([p((f["t"]/period,f["distances"][j])) for f in frames], color, 1.8, style)
        x = 289 + 91*j
        c.styled_path([(x,234),(x+28,234)], color, 1.8, style)
        c.label(x+35, 234, name, 14, "lm")
    c.save_figure(dest, "distances")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT/"data", help="Directory with the C++ output JSON")
    parser.add_argument("--output", type=Path, default=ROOT/"assets", help="Directory for GIF, PNG and SVG")
    args = parser.parse_args()
    def load(stem):
        return json.loads((args.data/f"{stem}.json").read_text(encoding="utf-8-sig"))
    acht, lagrange, convergence = (load(stem) for stem in ("figure-eight", "lagrange", "convergence"))
    args.output.mkdir(parents=True, exist_ok=True)
    trajectories(acht, lagrange, args.output)
    diagnostics(acht, convergence, args.output)
    distances(acht, args.output)
    motion(acht, args.output)


if __name__ == "__main__":
    main()
