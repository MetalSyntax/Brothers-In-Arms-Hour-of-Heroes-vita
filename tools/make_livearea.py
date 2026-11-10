#!/usr/bin/env python3
"""LiveArea images from the IPA's iTunesArtwork (512x512): icon0 128x128, bg0 840x500, startup 280x158,
pic0 960x544, all 8-bit indexed (psvita-toolkit livearea --validate).
Usage: python3 -I tools/make_livearea.py <iTunesArtwork> [extras/livearea]"""
import sys
from PIL import Image

src = Image.open(sys.argv[1]).convert('RGB')
out = sys.argv[2] if len(sys.argv) > 2 else 'extras/livearea'
bgcol = src.resize((1, 1), Image.LANCZOS).getpixel((0, 0))  # average colour for the side bars
dark = tuple(c // 3 for c in bgcol)

def framed(w, h, art_h):
    im = Image.new('RGB', (w, h), dark)
    a = src.resize((art_h, art_h), Image.LANCZOS)
    im.paste(a, ((w - art_h) // 2, (h - art_h) // 2))
    return im

def save(im, name, colors=256):
    im.quantize(colors, method=Image.Quantize.MEDIANCUT).save(f'{out}/{name}', optimize=True)

save(src.resize((128, 128), Image.LANCZOS), 'icon0.png')
save(framed(840, 500, 460), 'bg0.png', 64)  # bg0 must stay under 128 KB
save(framed(280, 158, 150), 'startup.png')
save(framed(960, 544, 520), 'pic0.png')
print('LiveArea images written to', out)
