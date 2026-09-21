"""Converts every .bmp in a folder to .png (needs Pillow). Optionally crops trailing empty background rows.
    python bmp2png.py folder [--crop]
"""
import glob
import os
import sys

from PIL import Image, ImageChops

folder = sys.argv[1] if len(sys.argv) > 1 else '.'
crop = '--crop' in sys.argv
for p in glob.glob(os.path.join(folder, '*.bmp')):
    im = Image.open(p).convert('RGB')
    if crop:   # drop the empty bottom (the background colour of the first pixel)
        bg = Image.new('RGB', im.size, im.getpixel((im.width - 1, im.height - 1)))
        box = ImageChops.difference(im, bg).getbbox()
        if box:
            im = im.crop((0, 0, im.width, min(im.height, box[3] + 12)))
    out = os.path.splitext(p)[0] + '.png'
    im.save(out)
    print('wrote', out, im.size)
