r"""Builds the README screenshots (docs/images/*.png) from offscreen renders. Nothing here touches a real window or the screen.

    powershell -File tools\ui_preview.ps1 -Out <folder>       (with the environment variable LSP_PREVIEW_CLEAN=1 for the tidy scene)
    python tools\make_readme_shots.py <folder> [nr_panel.bmp]

Every tab is cropped to its content and keeps the status bar from the bottom of the window. The Performance numbers in the
preview are sample data (there is no game or GPU sampling in an offscreen render); the README says so.
"""
import os
import sys

from PIL import Image, ImageChops

src = sys.argv[1]
nr_bmp = sys.argv[2] if len(sys.argv) > 2 else None
out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs', 'images')
os.makedirs(out, exist_ok=True)
STATUS = 62   # height of the status bar strip at the bottom, in pixels at the preview scale


def content_bottom(im):
    bg = im.getpixel((im.width // 2, im.height - STATUS - 20))
    body = im.crop((0, 60, im.width - 14, im.height - STATUS - 8))   # under the tab bar, above the status bar, clear of the scroll bar
    box = ImageChops.difference(body, Image.new('RGB', body.size, bg)).getbbox()
    return 60 + (box[3] if box else 0)


def tidy(name, dest, pad=22, bottom=None):
    im = Image.open(os.path.join(src, name)).convert('RGB')
    bottom = bottom or min(content_bottom(im) + pad, im.height - STATUS)
    top, bar = im.crop((0, 0, im.width, bottom)), im.crop((0, im.height - STATUS, im.width, im.height))
    res = Image.new('RGB', (im.width, top.height + bar.height))
    res.paste(top, (0, 0)); res.paste(bar, (0, top.height))
    res.save(os.path.join(out, dest), optimize=True)
    print('wrote', dest, res.size)


for name, dest in [('preview_addons.png', 'addons.png'), ('preview_performance.png', 'performance.png'),
                   ('preview_settings_tab.png', 'settings.png'), ('preview_logs.png', 'logs.png'), ('preview_about.png', 'about.png')]:
    tidy(name, dest, bottom={'performance.png': 905, 'logs.png': 350}.get(dest))   # these tabs end in a long empty panel

if nr_bmp and os.path.exists(nr_bmp):
    im = Image.open(nr_bmp).convert('RGB')
    bg = im.getpixel((im.width - 1, im.height - 1))
    box = ImageChops.difference(im, Image.new('RGB', im.size, bg)).getbbox()
    if box:
        im = im.crop((0, 0, im.width, min(im.height, box[3] + 16)))
    # only the top sections: the lower ones (games, frame detection, advanced) show this machine's program names and paths
    im = im.crop((0, 0, im.width, min(im.height, 1030)))
    im.save(os.path.join(out, 'neural-rendering-panel.png'), optimize=True)
    print('wrote neural-rendering-panel.png', im.size)
