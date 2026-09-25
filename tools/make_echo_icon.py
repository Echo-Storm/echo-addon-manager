"""Draws the Echo icon for LS Addon Manager (window, taskbar, tray): a dark rounded tile in the palette of the author's other
apps with a stack of frames (a bright one in front, two fainter echoes behind it). Writes a multi-size .ico (16..256, each size drawn on its
own with weights tuned for that size, not just scaled down) and a 256 px .png.

    python make_echo_icon.py [outdir]        default: the manager folder, next to manager-icon.png
    python make_echo_icon.py --preview file.png    also writes a contact sheet of the sizes on the dark UI background
"""
import math
import os
import sys

from PIL import Image, ImageDraw

BG_TOP = (36, 36, 36)
BG_BOT = (24, 24, 24)
BORDER = (74, 107, 40)         # #4a6b28
GREEN = (124, 179, 66)         # #7cb342
GREEN_HOT = (156, 204, 101)    # #9ccc65
GREEN_DIM = (74, 107, 40)      # #4a6b28
SS = 8                         # supersampling


def draw_icon(size):
    n = size * SS
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # tile: vertical gradient inside a rounded square, thin green border
    tile = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    grad = Image.new('RGBA', (n, n))
    gd = ImageDraw.Draw(grad)
    for y in range(n):
        t = y / max(1, n - 1)
        gd.line([(0, y), (n, y)], fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)) + (255,))
    radius = int(n * 0.22)
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, n - 1, n - 1], radius=radius, fill=255)
    tile.paste(grad, (0, 0), mask)
    img.alpha_composite(tile)
    bw = max(SS, int(n * (0.035 if size >= 32 else 0.06)))
    d.rounded_rectangle([bw // 2, bw // 2, n - 1 - bw // 2, n - 1 - bw // 2], radius=radius - bw // 2, outline=BORDER + (255,), width=bw)

    # the echo: a bright frame in front and two fainter copies stepping up and to the right behind it, like a motion afterimage
    # (frame generation: the real frame and its echoes)
    small = size <= 24
    side = n * (0.46 if not small else 0.50)
    step = n * (0.155 if not small else 0.17)
    x0 = n * 0.13
    y0 = n * 0.87 - side
    rad = side * 0.20
    lw = max(SS * 1.2, n * (0.045 if size >= 48 else (0.06 if size >= 32 else 0.085)))
    layers = [(2, GREEN_DIM, None), (1, GREEN, None), (0, GREEN_HOT, GREEN)]   # (index behind the front, outline, fill)
    if small:
        layers = [(1, GREEN, None), (0, GREEN_HOT, GREEN)]   # two frames are enough at 16-24 px
    for k, outline, fill in layers:
        x, y = x0 + step * k, y0 - step * k
        box = [x, y, x + side, y + side]
        if fill is not None:
            d.rounded_rectangle(box, radius=rad, fill=fill + (255,), outline=outline + (255,), width=int(lw))
        else:
            d.rounded_rectangle(box, radius=rad, fill=BG_BOT + (255,), outline=outline + (255,), width=int(lw))
    return img.resize((size, size), Image.LANCZOS)


def main():
    args = sys.argv[1:]
    preview = None
    if '--preview' in args:
        i = args.index('--preview')
        preview = args[i + 1]
        del args[i:i + 2]
    out = args[0] if args else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'manager')
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = {s: draw_icon(s) for s in sizes}
    frames[256].save(out + r'\manager-icon.png')
    frames[256].save(out + r'\manager-icon.ico', format='ICO', sizes=[(s, s) for s in sizes], append_images=[frames[s] for s in sizes if s != 256])
    print('wrote', out + r'\manager-icon.png', 'and', out + r'\manager-icon.ico', sizes)
    if preview:
        sheet = Image.new('RGB', (sum(sizes[:6]) * 3 + 30 + 300, 320), (24, 24, 24))
        x = 10
        for s in sizes[:6]:
            for k in (1, 3):   # actual size and 3x nearest-neighbour, to see the pixels
                im = frames[s] if k == 1 else frames[s].resize((s * 3, s * 3), Image.NEAREST)
                sheet.paste(im, (x, 10), im)
                x += im.width + 8
        sheet.paste(frames[128], (10, 180), frames[128])
        sheet.paste(frames[256].resize((128, 128), Image.LANCZOS), (170, 180), frames[256].resize((128, 128), Image.LANCZOS))
        sheet.save(preview)
        print('preview', preview)


main()
