"""Builds Workshop preview proposals (512x512) and installer icon proposals.

  python tools/make_previews.py <game resources> <captures folder> <output folder>

Captures (1280x720 windowed game screenshots): a.png (page 1), b_p2.png (page 2), c_plus.png ("+"
selected), d_p3.png (page 3), slide0..6.png (half size, page change animation).
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

RES, CAPS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)

PAPER = (236, 226, 226, 255)
INK = (55, 43, 45, 255)
RED = (200, 30, 30, 255)


def read_fnt(path):
    data = open(path, 'rb').read()
    pos, chars, line = 4, {}, 20
    while pos < len(data):
        btype = data[pos]
        size = struct.unpack_from('<I', data, pos + 1)[0]
        body = pos + 5
        if btype == 2:
            line = struct.unpack_from('<H', data, body)[0]
        if btype == 4:
            for i in range(size // 20):
                cid, x, y, w, h, xo, yo, xa, page, ch = struct.unpack_from('<IHHHHhhhBB', data, body + i * 20)
                chars[cid] = (x, y, w, h, xo, yo, xa)
        pos = body + size
    return chars, line


class BitmapFont:
    def __init__(self, name):
        self.chars, self.line = read_fnt(os.path.join(RES, 'font', name + '.fnt'))
        self.sheet = Image.open(os.path.join(RES, 'font', name + '_0.png')).convert('RGBA')

    def width(self, text, scale=1):
        return sum(self.chars.get(ord(c), self.chars[ord('?')])[6] for c in text) * scale

    def mask(self, text, scale=1):
        w, h = self.width(text) + 4, self.line + 4
        m = Image.new('L', (w, h), 0)
        x = 0
        for c in text:
            gx, gy, gw, gh, xo, yo, xa = self.chars.get(ord(c), self.chars[ord('?')])
            if gw and gh:
                g = self.sheet.crop((gx, gy, gx + gw, gy + gh)).getchannel('A')
                m.paste(g, (x + xo, yo), g)
            x += xa
        m = m.point(lambda a: 255 if a > 90 else 0)
        return m.resize((m.width * scale, m.height * scale), Image.NEAREST)

    def draw(self, img, text, center_x, y, scale, color, outline=None, shadow=True, max_width=None):
        m = self.mask(text, max(1, int(scale)))
        if max_width and m.width > max_width:
            m = m.resize((max_width, round(m.height * max_width / m.width)), Image.NEAREST)
        scale = max(1, int(scale))
        x = int(center_x - m.width / 2)
        if shadow:
            s = Image.new('RGBA', m.size, (0, 0, 0, 90))
            img.paste(s, (x + scale, y + scale), m)
        if outline:
            o = m.filter(ImageFilter.MaxFilter(2 * max(1, scale // 2) + 1))
            img.paste(Image.new('RGBA', m.size, outline), (x, y), o)
        img.paste(Image.new('RGBA', m.size, color), (x, y), m)


def cap(name):
    return Image.open(os.path.join(CAPS, name)).convert('RGBA')


def framed(im, w, border=4, tilt=0):
    """Screenshot as a paper card: scaled to width w with a dark border and a soft shadow."""
    h = round(im.height * w / im.width)
    im = im.resize((w, h), Image.LANCZOS)
    card = Image.new('RGBA', (w + border * 2, h + border * 2), INK)
    card.paste(im, (border, border))
    if tilt:
        card = card.rotate(tilt, resample=Image.BICUBIC, expand=True)
    shadow = Image.new('RGBA', (card.width + 16, card.height + 16), (0, 0, 0, 0))
    a = card.getchannel('A').point(lambda v: 110 if v else 0)
    shadow.paste(Image.new('RGBA', card.size, (0, 0, 0, 255)), (8, 10), a)
    shadow = shadow.filter(ImageFilter.GaussianBlur(5))
    shadow.alpha_composite(card, (0, 0))
    return shadow


def paper_bg(size=512):
    bg = Image.new('RGBA', (size, size), PAPER)
    # faint vignette like the game's menus
    v = Image.new('L', (size, size), 0)
    d = ImageDraw.Draw(v)
    d.ellipse((-size * 0.25, -size * 0.25, size * 1.25, size * 1.25), fill=255)
    v = v.filter(ImageFilter.GaussianBlur(60))
    dark = Image.new('RGBA', (size, size), (150, 135, 135, 255))
    return Image.composite(bg, dark, v)


up = BitmapFont('upheaval')
tm = BitmapFont('teammeatfont20bold')

# --- A: one big screenshot (page 2 with a Dead God file) between title bands
a = paper_bg()
up.draw(a, 'MORE SAVE SLOTS', 256, 26, 3, RED, outline=(255, 255, 255, 255), max_width=468)
shot = framed(cap('b_p2.png'), 480)
a.alpha_composite(shot, (256 - shot.width // 2 + 4, 112))
tm.draw(a, 'Up to 9 save files', 256, 400, 2, INK, shadow=False)
tm.draw(a, 'Requires REPENTOGON', 256, 450, 1, RED, shadow=False)
a.convert('RGB').save(os.path.join(OUT, 'preview_a.png'))

# --- B: the three pages stacked like sheets of paper
b = paper_bg()
up.draw(b, 'MORE SAVE SLOTS', 256, 26, 3, RED, outline=(255, 255, 255, 255), max_width=468)
for name, x, y, tilt in [('a.png', 24, 92, 4), ('b_p2.png', 96, 170, -3), ('d_p3.png', 168, 248, 2)]:
    card = framed(cap(name), 300, tilt=tilt)
    b.alpha_composite(card, (x, y))
tm.draw(b, 'FILE 1 - 9 in pages of 3', 256, 442, 1, INK, shadow=False)
tm.draw(b, 'Requires REPENTOGON', 256, 470, 1, RED, shadow=False)
b.convert('RGB').save(os.path.join(OUT, 'preview_b.png'))

# --- C: the page change (two pages side by side with the slide arrow and page dots)
c = paper_bg()
up.draw(c, 'MORE SAVE SLOTS', 256, 26, 3, RED, outline=(255, 255, 255, 255), max_width=468)
p1 = cap('a.png').crop((420, 0, 1280, 620))
p2 = cap('b_p2.png').crop((0, 0, 860, 620))
strip = Image.new('RGBA', (p1.width + p2.width, 620), PAPER)
strip.paste(p1, (0, 0))
strip.paste(p2, (p1.width, 0))
strip = framed(strip, 490)
c.alpha_composite(strip, (256 - strip.width // 2 + 4, 140))
d = ImageDraw.Draw(c)
cx, cy = 256, 140 + strip.height // 2 - 6
d.polygon([(cx - 18, cy - 26), (cx + 22, cy), (cx - 18, cy + 26)], fill=RED, outline=(255, 255, 255, 255))
tm.draw(c, 'Slide between pages of 3 files', 256, 420, 1, INK, shadow=False)
tm.draw(c, 'Requires REPENTOGON', 256, 452, 1, RED, shadow=False)
c.convert('RGB').save(os.path.join(OUT, 'preview_c.png'))

# --- icon candidates: item sprites upscaled (pixel art, no smoothing)
for item in ['085_deckofcards', '362_lilchest', '198_box', '719_keepersbox']:
    sprite = Image.open(os.path.join(RES, 'gfx', 'items', 'collectibles', f'collectibles_{item}.png')).convert('RGBA')
    box = sprite.getbbox()
    sprite = sprite.crop(box)
    side = max(sprite.size)
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    sq.paste(sprite, ((side - sprite.width) // 2, (side - sprite.height) // 2))
    sq.resize((256, 256), Image.NEAREST).save(os.path.join(OUT, f'icon_{item.split("_", 1)[1]}.png'))

print('ok:', OUT)
