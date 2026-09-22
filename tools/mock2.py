"""
Themed 480x320 mockups, pixel-accurate to the sketch's geometry.

Text is approximated with Consolas at the GFX advance (6*size px); on hardware
it is the Adafruit GFX 6x8 built-in font, so the boxes match and the
letterforms differ a little.
"""
import os

from PIL import Image, ImageDraw, ImageFont

from themes import THEMES
import glyphs

PX = 40
OUT = os.path.dirname(os.path.abspath(__file__))
F = {}


def font(size):
    if size not in F:
        F[size] = ImageFont.truetype(r"C:\Windows\Fonts\consola.ttf", size * 8)
    return F[size]


def txt(d, x, y, s, size, col):
    d.text((x, y - size), s, font=font(size), fill=col)


def txt_mid(d, x, w, y, s, size, col):
    txt(d, x + (w - len(s) * 6 * size) // 2, y, s, size, col)


# --- position: 1.e4 e5 2.Nf3 Nc6, White's f1 bishop picked up --------------
START = ("rnbqkbnr" "pppppppp" "........" "........"
         "........" "........" "PPPPPPPP" "RNBQKBNR")
BOARD = list(START)
for a, b in ((52, 36), (12, 28), (62, 45), (1, 18)):
    BOARD[b] = BOARD[a]
    BOARD[a] = "."
SEL_FROM = 61
TARGETS = [52, 43, 34, 25, 16]
LAST_FROM, LAST_TO = 1, 18
IDX = {"P": 0, "N": 1, "B": 2, "R": 3, "Q": 4, "K": 5}

SEL = (96, 200, 110)
TARGET = (60, 170, 95)
LASTMOVE = (216, 190, 72)


def draw(theme, flip=False, portrait=False):
    (name, lsq, dsq, frame, pbg, card, chi, text, dim, acc, wc, bc) = theme
    pieces = glyphs.piece_pixels(wc, bc, PX)

    W, H = (320, 480) if portrait else (480, 320)
    img = Image.new("RGB", (W, H), pbg)
    d = ImageDraw.Draw(img)

    # board
    for r in range(8):
        for c in range(8):
            sq = c + r * 8
            sc, sr = (7 - c, 7 - r) if flip else (c, r)
            x, y = sc * PX, sr * PX
            light = ((c + r) & 1) == 0
            d.rectangle([x, y, x + PX - 1, y + PX - 1], fill=lsq if light else dsq)
            # coordinates: the opposite square colour, pulled toward the
            # square it sits on so it stays a hint rather than a label
            other = dsq if light else lsq
            here = lsq if light else dsq
            tint = tuple(int(other[i] * 0.72 + here[i] * 0.28) for i in range(3))
            if sr == 7:
                txt(d, x + PX - 7, y + PX - 9, "abcdefgh"[c], 1, tint)
            if sc == 0:
                txt(d, x + 3, y + 3, "87654321"[r], 1, tint)
            if sq in (LAST_FROM, LAST_TO):
                for k in range(2):
                    d.rectangle([x + k, y + k, x + PX - 1 - k, y + PX - 1 - k],
                                outline=LASTMOVE)
            if sq == SEL_FROM:
                for k in range(3):
                    d.rectangle([x + k, y + k, x + PX - 1 - k, y + PX - 1 - k],
                                outline=SEL)
            p = BOARD[sq]
            if p != ".":
                glyphs.blit(img, pieces[IDX[p.upper()] + (0 if p.isupper() else 6)],
                            x, y, PX)

    for sq in TARGETS:
        c, r = sq % 8, sq // 8
        sc, sr = (7 - c, 7 - r) if flip else (c, r)
        cx, cy = sc * PX + PX // 2, sr * PX + PX // 2
        if BOARD[sq] == ".":
            d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=TARGET)
        else:
            for k in range(3):
                d.ellipse([cx - 17 + k, cy - 17 + k, cx + 17 - k, cy + 17 - k],
                          outline=TARGET)

    # frame around the board, as in the reference
    d.rectangle([0, 0, 319, 319], outline=frame)

    def button(x, y, w, h, label, primary=False, enabled=True):
        bg = acc if primary else (card if enabled else pbg)
        fg = pbg if primary else (text if enabled else dim)
        d.rectangle([x, y, x + w - 1, y + h - 1], fill=bg,
                    outline=acc if primary else chi)
        txt_mid(d, x, w, y + (h - 16) // 2, label, 2, fg)

    if portrait:
        oy = 320
        d.rectangle([0, oy, 319, oy + 159], fill=pbg)
        txt_mid(d, 0, 320, oy + 8, "MEGACHESS", 3, acc)
        d.line([12, oy + 38, 307, oy + 38], fill=chi)
        d.rectangle([12, oy + 46, 180, oy + 80], fill=card, outline=chi)
        txt(d, 24, oy + 55, "WHITE - YOUR MOVE", 1, text)
        txt(d, 24, oy + 67, "move 5    b8-c6", 1, dim)
        button(192, oy + 46, 115, 34, "UNDO")
        button(12, oy + 92, 148, 38, "MENU")
        button(172, oy + 92, 135, 38, "NEW", True)
        txt_mid(d, 0, 320, oy + 142, "depth 3   SD   %s" % name, 1, dim)
        return img

    ox, w = 320, 160
    d.rectangle([ox, 0, ox + w - 1, 319], fill=pbg)
    txt_mid(d, ox, w, 10, "MEGACHESS", 2, acc)
    d.line([ox + 10, 32, ox + w - 11, 32], fill=chi)
    txt_mid(d, ox, w, 40, "1.9", 2, text)
    d.line([ox + 10, 62, ox + w - 11, 62], fill=chi)

    d.rectangle([ox + 10, 72, ox + w - 11, 106], fill=card, outline=chi)
    txt_mid(d, ox, w, 82, "YOUR MOVE", 1, text)
    txt_mid(d, ox, w, 94, "white  move 5", 1, dim)

    txt(d, ox + 10, 120, "CAPTURED", 1, dim)
    d.rectangle([ox + 10, 132, ox + w - 11, 168], outline=chi)

    button(ox + 10, 186, 140, 38, "UNDO")
    button(ox + 10, 232, 140, 38, "MENU")
    button(ox + 10, 278, 140, 38, "NEW", True)
    return img


def menu(theme):
    (name, lsq, dsq, frame, pbg, card, chi, text, dim, acc, wc, bc) = theme
    img = Image.new("RGB", (480, 320), pbg)
    d = ImageDraw.Draw(img)

    def button(x, y, w, h, label, primary=False, enabled=True):
        bg = acc if primary else (card if enabled else pbg)
        fg = pbg if primary else (text if enabled else dim)
        d.rectangle([x, y, x + w - 1, y + h - 1], fill=bg,
                    outline=acc if primary else chi)
        txt_mid(d, x, w, y + (h - 16) // 2, label, 2, fg)

    txt_mid(d, 0, 480, 12, "MEGACHESS", 4, acc)
    txt_mid(d, 0, 480, 50, 'mega 2560   3.5" touch', 1, dim)

    txt(d, 20, 74, "GAME MODE", 1, dim)
    button(20, 88, 146, 42, "VS ENGINE", True)
    button(176, 88, 146, 42, "2 PLAYER")
    button(332, 88, 146, 42, "DEMO")

    txt(d, 20, 146, "DEPTH", 1, dim)
    button(20, 160, 42, 42, "-")
    d.rectangle([68, 160, 131, 201], fill=card, outline=chi)
    txt_mid(d, 68, 64, 172, "3", 3, text)
    button(138, 160, 42, 42, "+")

    txt(d, 196, 146, "YOU PLAY", 1, dim)
    button(196, 160, 92, 42, "WHITE")
    txt(d, 296, 146, "THEME", 1, dim)
    button(296, 160, 90, 42, name[:6])
    txt(d, 394, 146, "BOARD", 1, dim)
    button(394, 160, 84, 42, "NORM")

    button(20, 222, 210, 44, "RESUME")
    button(250, 222, 228, 44, "NEW GAME", True)
    txt_mid(d, 0, 480, 286, "SD ready - 4 piece sets, saving and PGN log on",
            1, dim)
    return img


def main():
    rows = []
    for t in THEMES[:3]:
        rows.append(draw(t))
    sheet = Image.new("RGB", (480, 320 * len(rows) + 8 * (len(rows) - 1)),
                      (70, 70, 70))
    for i, im in enumerate(rows):
        sheet.paste(im, (0, i * 328))
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(
        os.path.join(OUT, "themes.png"))

    menus = [menu(t) for t in THEMES[:3]]
    ms = Image.new("RGB", (480, 320 * 3 + 16), (70, 70, 70))
    for i, im in enumerate(menus):
        ms.paste(im, (0, i * 328))
    ms.resize((ms.width * 2, ms.height * 2), Image.NEAREST).save(
        os.path.join(OUT, "menus.png"))

    # orientation options, all in theme 0
    alt = [draw(THEMES[0], flip=True), draw(THEMES[0], portrait=True)]
    a = Image.new("RGB", (480 + 320 + 8, 480), (70, 70, 70))
    a.paste(alt[0], (0, 0))
    a.paste(alt[1], (488, 0))
    a.resize((a.width * 2, a.height * 2), Image.NEAREST).save(
        os.path.join(OUT, "orientations.png"))
    print("themes.png  menus.png  orientations.png")


main()
