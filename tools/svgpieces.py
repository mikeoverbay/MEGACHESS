"""
Rasterise lichess piece-set SVGs into Megachess .SET artwork.

Each SVG is recoloured per theme by substituting its fills/strokes (they are
plain hex values), rendered at 8x with skia (Chrome's rasteriser, real alpha), then LANCZOS-
downsampled to 40x40.
"""
import io, os, re, struct, sys
from PIL import Image
from themes import THEMES

PX = 40
SS = 8
PIECES = ["P", "N", "B", "R", "Q", "K"]          # .SET order, pawn..king
SETS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "svg")


def lum(hexcol):
    h = hexcol.lstrip("#")
    if len(h) == 3: h = "".join(c * 2 for c in h)
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return 0.299 * r + 0.587 * g + 0.114 * b


def hx(rgb):
    return "#%02x%02x%02x" % tuple(int(v) for v in rgb)


STROKE_MULT = {}    # per-theme overrides; default 1.8 for black pieces   # black pieces only
# Outward halo, in final pixels, drawn in the theme's black-rim colour under
# the piece. Dilates the silhouette rather than relying on the SVG stroke, so
# the ring is a guaranteed width and never eats into the piece.
HALO_PX = {}        # per-theme overrides; default 0


def _dilate(a, r):
    """8-neighbour max-dilation of a uint8 alpha plane by r pixels."""
    import numpy as np
    out = a
    for _ in range(r):
        m = out.copy()
        m[1:, :]    = np.maximum(m[1:, :],    out[:-1, :])
        m[:-1, :]   = np.maximum(m[:-1, :],   out[1:, :])
        m[:, 1:]    = np.maximum(m[:, 1:],    out[:, :-1])
        m[:, :-1]   = np.maximum(m[:, :-1],   out[:, 1:])
        m[1:, 1:]   = np.maximum(m[1:, 1:],   out[:-1, :-1])
        m[1:, :-1]  = np.maximum(m[1:, :-1],  out[:-1, 1:])
        m[:-1, 1:]  = np.maximum(m[:-1, 1:],  out[1:, :-1])
        m[:-1, :-1] = np.maximum(m[:-1, :-1], out[1:, 1:])
        out = m
    return out

def recolour(svg_text, white_piece, wc, bc, stroke_mult=1.8):
    """
    wc / bc = (top, bottom, rim, detail) from themes.py.
    Light fills become the body, dark strokes/fills become the edge, for a
    white piece; for a black piece the dark body takes the theme's dark
    body, its strokes take the (light) rim, and its light highlights take
    the detail colour.
    """
    top, bottom, rim, detail = wc if white_piece else bc
    body = tuple((top[i] + bottom[i]) // 2 for i in range(3))

    def sub(m):
        attr, val = m.group(1), m.group(2)
        if val in ("none", "currentColor"): return m.group(0)
        try: L = lum(val)
        except ValueError: return m.group(0)
        light = L > 128
        if white_piece:
            new = body if light else rim
        else:
            if attr == "stroke": new = rim
            else:                new = detail if light else body
        return '%s="%s"' % (attr, hx(new))

    out = re.sub(r'\b(fill|stroke)="(#[0-9a-fA-F]{3,6}|[a-zA-Z]+)"', sub, svg_text)


    # Alpha's strokes are ~1.5 units on a 45-unit box, about 1.3px at 40px.

    # Fine as a dark outline on a light body, but far too thin to be the

    # LIGHT rim that has to carry a black piece across a near-black square.

    # Thicken black pieces' strokes; leave white pieces alone.

    if not white_piece:

        out = re.sub(r'stroke-width="([0-9.]+)"',

                     lambda w: 'stroke-width="%.2f"' % (float(w.group(1)) * stroke_mult), out)

    return out
def render(svg_text, halo_px=0, halo_col=None, size=None):
    """-> PIL RGBA at PX x PX with real alpha. Skia is the rasteriser Chrome
    uses, so this is reference-quality and handles every construct in the
    lichess sets (clip-paths, evenodd fills, round joins). With halo_px, a
    ring of halo_col is laid UNDER the piece, halo_px wide (final pixels),
    by dilating the silhouette at the supersampled resolution."""
    import skia, numpy as np
    size = size or PX
    big = size * SS
    dom = skia.SVGDOM.MakeFromStream(skia.MemoryStream(svg_text.encode("utf-8"), True))
    if dom is None:
        raise RuntimeError("skia could not parse SVG")
    dom.setContainerSize(skia.Size(big, big))
    surface = skia.Surface(big, big)
    canvas = surface.getCanvas()
    canvas.clear(skia.Color4f(0, 0, 0, 0))
    dom.render(canvas)
    arr = surface.makeImageSnapshot().toarray(colorType=skia.kRGBA_8888_ColorType)
    top = Image.fromarray(arr, "RGBA")
    if halo_px > 0 and halo_col is not None:
        a = arr[:, :, 3]
        ring = np.clip(_dilate(a, halo_px * SS).astype(np.int16) - a.astype(np.int16), 0, 255).astype(np.uint8)
        halo = np.zeros_like(arr)
        halo[:, :, 0], halo[:, :, 1], halo[:, :, 2] = halo_col
        halo[:, :, 3] = ring
        top = Image.alpha_composite(Image.fromarray(halo, "RGBA"), top)
    return top.resize((size, size), Image.LANCZOS)


def piece_images(set_name, wc, bc, stroke_mult=1.8, halo_px=0):
    """12 RGBA images, WP..WK then BP..BK."""
    out = []
    for side, white in (("w", True), ("b", False)):
        for p in PIECES:
            path = os.path.join(SETS_DIR, set_name, side + p + ".svg")
            txt = open(path, encoding="utf-8").read()
            out.append(render(recolour(txt, white, wc, bc, stroke_mult),
                              0 if white else halo_px, bc[2]))
    return out


def write_set(theme, set_name, dest_dir):
    (name, lsq, dsq, frame, pbg, card, chi, text, dim, acc, wc, bc) = theme
    imgs = piece_images(set_name, wc, bc, STROKE_MULT.get(name, 1.8), HALO_PX.get(name, 0))
    blob = bytearray(b"MCPS") + struct.pack("<HH", 1, PX)
    blob += name.encode("ascii")[:16].ljust(16, b"\0") + b"\0" * 8
    for im in imgs:
        px = im.load()
        for y in range(PX):
            for x in range(PX):
                r, g, b, a = px[x, y]
                blob += struct.pack("<HB", ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3), a)
    os.makedirs(dest_dir, exist_ok=True)
    path = os.path.join(dest_dir, name + ".SET")
    open(path, "wb").write(blob)
    return path, len(blob)


def preview(set_names, theme, out_path):
    (name, lsq, dsq, *_ , wc, bc) = theme
    rows = len(set_names) * 2
    sheet = Image.new("RGB", (PX * 12, PX * rows + 4 * (len(set_names) - 1)), (70, 70, 70))
    y = 0
    for s in set_names:
        imgs = piece_images(s, wc, bc)
        for row, bg in ((0, dsq), (1, lsq)):
            strip = Image.new("RGB", (PX * 12, PX), bg)
            for i, im in enumerate(imgs):
                strip.paste(im, (i * PX, 0), im)
            sheet.paste(strip, (0, y + row * PX))
        y += PX * 2 + 4
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(out_path)


if __name__ == "__main__":
    sets = sys.argv[1].split(",") if len(sys.argv) > 1 else ["cburnett", "merida", "alpha"]
    chosen = sys.argv[2] if len(sys.argv) > 2 else None
    dests = sys.argv[3:] if len(sys.argv) > 3 else []

    amber = [t for t in THEMES if t[0] == "AMBER"][0]
    preview(sets, amber, "sets_svg_preview.png")
    print("preview -> sets_svg_preview.png  (rows per set: dark square, light square)")

    if chosen:
        for t in THEMES:
            for d in dests:
                p, n = write_set(t, chosen, d)
                print("  %-40s %6d bytes" % (p, n))
