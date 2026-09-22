r"""
Convert an image into Megachess's boot splash: /IMAGES/SPLASH.IMG

    python tools/splash.py my_splash.png            -> SPLASH.IMG next to it
    python tools/splash.py my_splash.png H:\         -> straight onto the card

Any format PIL reads. Resized and centre-cropped to 480x320 if it isn't
already (you will be told). Picks run-length or raw, whichever is smaller, and
reports how long the Mega will take to paint it - the card streams at roughly
50-100 KB/s, so flat colour areas are cheap and photos are not.

File layout: "MCSP", mode u8 (0 raw / 1 rle), w u16, h u16, padded to 16
bytes, then pixels. Raw: RGB565 little-endian. RLE: [run u8 1..255][RGB565 u16].
"""
import os
import struct
import sys

from PIL import Image

W, H = 480, 320


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    src = sys.argv[1]
    dest = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(src))

    im = Image.open(src).convert("RGB")
    if im.size != (W, H):
        print("note: %dx%d -> fitting to %dx%d" % (im.size[0], im.size[1], W, H))
        s = max(W / im.size[0], H / im.size[1])          # scale to cover
        im = im.resize((round(im.size[0] * s), round(im.size[1] * s)), Image.LANCZOS)
        l, t = (im.size[0] - W) // 2, (im.size[1] - H) // 2
        im = im.crop((l, t, l + W, t + H))               # then centre-crop

    px = [rgb565(*im.getpixel((x, y))) for y in range(H) for x in range(W)]

    raw = b"".join(struct.pack("<H", c) for c in px)

    rle = bytearray()
    i = 0
    while i < len(px):
        c, n = px[i], 1
        while i + n < len(px) and px[i + n] == c and n < 255:
            n += 1
        rle += struct.pack("<BH", n, c)
        i += n

    mode, body = (1, bytes(rle)) if len(rle) < len(raw) else (0, raw)
    hdr = (b"MCSP" + struct.pack("<BHH", mode, W, H)).ljust(16, b"\0")

    if os.path.isdir(dest):
        os.makedirs(os.path.join(dest, "MEGACHESS"), exist_ok=True)
        out = os.path.join(dest, "MEGACHESS", "SPLASH.IMG")
    else:
        out = dest
    with open(out, "wb") as f:
        f.write(hdr + body)

    kb = (16 + len(body)) / 1024.0
    print("%s  %s  %.0f KB  (raw would be %.0f KB)"
          % (out, "RLE" if mode else "RAW", kb, len(raw) / 1024.0))
    print("paints in roughly %.1f-%.1f s on the Mega" % (kb / 100.0, kb / 50.0))


if __name__ == "__main__":
    main()
