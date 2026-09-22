"""
Single source of truth for Megachess themes.

Each theme carries the board colours, the panel chrome and the two piece
colourways. gen_sets.py writes a .SET per theme onto the card; mockup.py draws
from the same table; and emit_c() prints the matching C table for megachess.h,
so the three can never drift apart.
"""

# key: (light_sq, dark_sq, frame,
#       panel_bg, card, card_hi, text, text_dim, accent,
#       (w_top, w_bot, w_edge), (b_top, b_bot, b_edge))
THEMES_RAW = [
    ("CLASSIC",
     (237, 234, 214), (125, 155, 118), (237, 234, 214),
     (26, 30, 28), (42, 48, 44), (58, 66, 60),
     (237, 234, 214), (138, 152, 142), (237, 234, 214),
     ((248, 246, 236), (216, 213, 196), (22, 26, 22), (22, 26, 22)),
     ((58, 64, 56), (20, 24, 20), (198, 204, 188), (116, 124, 110))),

    # AMBER re-stacked to SLATE's luminance offsets. The old near-black dark
    # square left no room below it for a dark body, which is why black pieces
    # were mud on the panel no matter how the rim was pushed. Mid-tone dark
    # square, dark body ~65 below it, light rim ~85 above it - Slate's recipe.
    # AMBER: Slate's luminance stack (that is what makes black pieces read on
    # the dark square) but at full chroma - the first re-stack got there by
    # adding grey and came out pastel. Same gaps, saturated colours.
    ("AMBER",
     (252, 182, 52), (138, 80, 16), (255, 196, 70),
     (18, 14, 8), (46, 33, 12), (66, 47, 16),
     (240, 169, 59), (150, 108, 44), (240, 169, 59),
     ((255, 214, 120), (238, 178, 72), (28, 16, 4), (28, 16, 4)),
     ((56, 36, 10), (36, 22, 6), (240, 168, 56), (140, 92, 30))),

    ("POCKET",
     (200, 230, 190), (127, 191, 106), (206, 234, 196),
     (24, 30, 22), (40, 50, 36), (56, 68, 50),
     (206, 234, 196), (126, 152, 118), (184, 224, 168),
     ((230, 246, 222), (198, 228, 188), (20, 36, 16), (20, 36, 16)),
     ((48, 68, 40), (20, 32, 16), (176, 210, 166), (104, 138, 94))),

    ("SLATE",
     (222, 228, 236), (108, 124, 146), (226, 232, 240),
     (18, 21, 26), (34, 40, 50), (50, 58, 72),
     (230, 236, 244), (132, 144, 162), (108, 168, 240),
     ((250, 252, 255), (214, 222, 234), (20, 28, 40), (20, 28, 40)),
     ((70, 82, 98), (30, 38, 50), (194, 208, 226), (116, 130, 150))),

    # P1-phosphor green on black, same luminance stack as SLATE.
    ("VECTOR",
     (120, 235, 100), (48, 120, 44), (130, 240, 110),
     (6, 14, 8), (14, 30, 16), (22, 46, 26),
     (150, 255, 140), (80, 150, 80), (100, 255, 100),
     ((200, 255, 190), (150, 240, 140), (10, 40, 14), (10, 40, 14)),
     ((14, 44, 18), (6, 26, 10), (160, 255, 150), (70, 140, 70))),
]


# ---------------------------------------------------------------------------
# Panel colour compensation.
#
# The DIYables HX8357D renders our ambers a step toward yellow - an orange-
# brown accent reads as pale yellow on the glass. Gamma cannot fix that (it
# is a tone curve; a yellow stays a yellow at any brightness), so this is a
# HUE rotation: colours in the yellow-orange band are turned toward orange
# by PANEL_HUE_SHIFT degrees, with HSL lightness preserved so the legibility
# gaps that make black pieces readable do not move. Greens and blues are
# outside the band and untouched. Applied once here, so the firmware table,
# the card artwork and the simulator all agree.
#
# Tune the one number. Negative = toward orange/brown, positive = yellow.
# ---------------------------------------------------------------------------
import colorsys

PANEL_HUE_SHIFT = -14.0          # degrees
HUE_BAND        = (15.0, 75.0)   # only hues in this range are turned
HUE_FEATHER     = 8.0            # soft edge so the band has no cliff
MIN_SATURATION  = 0.45           # leave creams and greys alone


def _luma(r, g, b): return 0.299 * r + 0.587 * g + 0.114 * b


def _corrected(rgb):
    r, g, b = [v / 255.0 for v in rgb]
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    hd = h * 360.0
    lo, hi = HUE_BAND
    # Only saturated colours in the band: creams (Classic's light square)
    # sit in the band by hue but must not turn peach.
    if s < MIN_SATURATION or hd < lo - HUE_FEATHER or hd > hi + HUE_FEATHER:
        return tuple(rgb)
    w = 1.0
    if hd < lo:   w = (hd - (lo - HUE_FEATHER)) / HUE_FEATHER
    elif hd > hi: w = ((hi + HUE_FEATHER) - hd) / HUE_FEATHER
    hd = (hd + PANEL_HUE_SHIFT * w) % 360.0
    nr, ng, nb = colorsys.hls_to_rgb(hd / 360.0, l, s)
    # HSL lightness is not luma - green carries 59% of it - so turning toward
    # orange darkens the colour. Rescale so luma matches the authored value
    # and the legibility gaps come through untouched.
    y0, y1 = _luma(r, g, b), _luma(nr, ng, nb)
    if y1 > 0:
        k = y0 / y1
        nr, ng, nb = min(1.0, nr * k), min(1.0, ng * k), min(1.0, nb * k)
    return (int(round(nr * 255)), int(round(ng * 255)), int(round(nb * 255)))


def _correct_theme(t):
    out = [t[0]]
    for v in t[1:]:
        if isinstance(v[0], tuple):          # a piece colourway: tuple of colours
            out.append(tuple(_corrected(c) for c in v))
        else:
            out.append(_corrected(v))
    return tuple(out)


THEMES = [_correct_theme(t) for t in THEMES_RAW]


def emit_c():
    """Print the C theme table for megachess.h."""
    def rgb(c):
        return "RGB(%3d,%3d,%3d)" % c

    out = []
    out.append("#define THEME_COUNT %d" % len(THEMES))
    out.append("")
    out.append("struct Theme {")
    out.append("    char     name[8];")
    out.append("    uint16_t lightSq, darkSq, frame;")
    out.append("    uint16_t panelBg, card, cardHi;")
    out.append("    uint16_t text, textDim, accent;")
    out.append("    uint16_t wFill, wEdge, bFill, bEdge;")
    out.append("};")
    out.append("")
    out.append("static const Theme themes[THEME_COUNT] PROGMEM = {")
    for t in THEMES:
        (name, lsq, dsq, frame, pbg, card, chi, text, dim, acc, wc, bc) = t
        # the flash fallback glyphs are flat, so take the body's midpoint
        wf = tuple((wc[0][i] + wc[1][i]) // 2 for i in range(3))
        bf = tuple((bc[0][i] + bc[1][i]) // 2 for i in range(3))
        # flash fallback is two flat 1-bit layers: body, then edge ink
        we, be = wc[2], bc[3]
        out.append('    { "%s",' % name)
        out.append("      %s, %s, %s," % (rgb(lsq), rgb(dsq), rgb(frame)))
        out.append("      %s, %s, %s," % (rgb(pbg), rgb(card), rgb(chi)))
        out.append("      %s, %s, %s," % (rgb(text), rgb(dim), rgb(acc)))
        out.append("      %s, %s, %s, %s }," %
                   (rgb(wf), rgb(we), rgb(bf), rgb(be)))
    out.append("};")
    return "\n".join(out)


if __name__ == "__main__":
    print(emit_c())
