"""
Shared glyph rasteriser: Segoe UI Symbol chess glyphs -> RGBA pixel lists.

Both the .SET writer and the mockups pull from here, so the artwork on the card
is byte-for-byte what the mockups showed.

The outline glyph (U+2654..) is split into two layers rather than painted as
one. Its strokes are thick at 40px, and painting them all in a single colour
gave black pieces a heavy light-coloured border that read as WHITE on a light
square. So:

    rim     - the body's own outer boundary. Goes dark for BOTH sides, which
              is what a real chess set does: a black piece is a black
              silhouette, not a light-edged one.
    detail  - outline strokes strictly inside the rim: crown points, the
              knight's eye, the mitre slit. This is the layer that goes light
              on black pieces, and it is what keeps them legible.

The split is computed at 4x the final size so that thin features (a queen's
crown spikes, a pawn's neck) survive the erosion instead of being wiped out.
"""
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

FONT = r"C:\Windows\Fonts\seguisym.ttf"
SS = 8        # supersample for the initial glyph render
WORK = 4      # working multiple of the final size for the rim/detail split
SOLID = "\u265A\u265B\u265C\u265D\u265E\u265F"   # K Q R B N P filled
OUTLN = "\u2654\u2655\u2656\u2657\u2658\u2659"   # K Q R B N P outlined
ORDER = [5, 4, 3, 2, 1, 0]                        # emit pawn..king

_cache = {}


def _layers(px):
    """-> (body, rim, detail) alpha maps at px, one triple per piece type."""
    if px in _cache:
        return _cache[px]

    canvas = px * SS
    font = ImageFont.truetype(FONT, int(canvas * 0.78))

    def render(ch):
        im = Image.new("L", (canvas, canvas), 0)
        ImageDraw.Draw(im).text((canvas // 2, canvas // 2), ch, font=font,
                                fill=255, anchor="mm")
        return im

    def carve(img, box, to):
        l, t, r, b = box
        w, h = r - l, b - t
        side = max(w, h)
        sq = Image.new("L", (side, side), 0)
        sq.paste(img.crop(box), ((side - w) // 2, (side - h) // 2))
        m = int(side * 0.06)
        out = Image.new("L", (side + 2 * m, side + 2 * m), 0)
        out.paste(sq, (m, m))
        return out.resize((to, to), Image.LANCZOS)

    hi = px * WORK
    rim_px = WORK                      # ~1px once downsampled
    result = []
    for gi in ORDER:
        s, o = render(SOLID[gi]), render(OUTLN[gi])
        bs, bo = s.getbbox(), o.getbbox()
        box = (min(bs[0], bo[0]), min(bs[1], bo[1]),
               max(bs[2], bo[2]), max(bs[3], bo[3]))

        body_hi = carve(s, box, hi)
        line_hi = carve(o, box, hi)

        inner = body_hi.filter(ImageFilter.MinFilter(2 * rim_px + 1))
        rim_hi = ImageChops.subtract(body_hi, inner)
        det_hi = ImageChops.darker(line_hi, inner)

        # Outline strokes that fall outside the solid body still belong to the
        # silhouette, so fold them into the rim rather than losing them.
        outside = ImageChops.subtract(line_hi, body_hi)
        rim_hi = ImageChops.lighter(rim_hi, outside)

        result.append((
            ImageChops.lighter(body_hi, line_hi).resize((px, px), Image.LANCZOS),
            rim_hi.resize((px, px), Image.LANCZOS),
            det_hi.resize((px, px), Image.LANCZOS)))

    _cache[px] = result
    return result


def _compose(layers, top, bottom, rim_c, det_c, px):
    body, rim, det = layers
    bp, rp, dp = body.load(), rim.load(), det.load()
    out = []
    for y in range(px):
        t = y / float(px - 1)
        br = int(top[0] + (bottom[0] - top[0]) * t)
        bg = int(top[1] + (bottom[1] - top[1]) * t)
        bb = int(top[2] + (bottom[2] - top[2]) * t)
        for x in range(px):
            a = bp[x, y]
            if a == 0:
                out.append((0, 0, 0, 0))
                continue
            r, g, b = br, bg, bb
            k = rp[x, y] / 255.0
            if k:
                r = int(r + (rim_c[0] - r) * k)
                g = int(g + (rim_c[1] - g) * k)
                b = int(b + (rim_c[2] - b) * k)
            k = dp[x, y] / 255.0
            if k:
                r = int(r + (det_c[0] - r) * k)
                g = int(g + (det_c[1] - g) * k)
                b = int(b + (det_c[2] - b) * k)
            out.append((r, g, b, a))
    return out


def piece_pixels(white, black, px):
    """
    white/black are (top, bottom, rim, detail) RGB triples.
    -> 12 pieces, WP..WK then BP..BK, each a flat list of (r, g, b, a).
    """
    L = _layers(px)
    out = []
    for colours in (white, black):
        for i in range(6):
            out.append(_compose(L[i], *colours, px=px))
    return out


def blit(img, pixels, x, y, px):
    for yy in range(px):
        for xx in range(px):
            r, g, b, a = pixels[yy * px + xx]
            if not a:
                continue
            dr, dg, db = img.getpixel((x + xx, y + yy))
            f = a / 255.0
            img.putpixel((x + xx, y + yy),
                         (int(r * f + dr * (1 - f)),
                          int(g * f + dg * (1 - f)),
                          int(b * f + db * (1 - f))))
