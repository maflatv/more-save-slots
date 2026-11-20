"""Genera los gráficos de Más Ranuras a partir de los recursos del juego.

  python tools/generar_graficos.py <carpeta resources del juego> <carpeta de salida>

Salida:
  saveselectmenu_p2.png / _p3.png  hoja del menú de partidas con "FILE 4-6" / "FILE 7-9"
  mas_ranuras.png + mas_ranuras.anm2  pestañas laterales (◀, ▶, +) e indicador de página
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

RES, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)

SHEET = os.path.join(RES, 'gfx', 'ui', 'main menu', 'saveselectmenu.png')
FONT_FNT = os.path.join(RES, 'font', 'teammeatfont20bold.fnt')
FONT_PNG = os.path.join(RES, 'font', 'teammeatfont20bold_0.png')

# Zona del rótulo "FILE N" de cada tarjeta en la hoja (x0, y0, x1, y1).
LABELS = [(20, 408, 146, 443), (165, 408, 292, 443), (312, 408, 441, 443)]


def is_ink(px):
    return px[3] > 0 and sum(px[:3]) < 250


def components(im, box):
    """Componentes conexas de tinta dentro de box."""
    x0, y0, x1, y1 = box
    seen = set()
    comps = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            if (x, y) in seen or not is_ink(im.getpixel((x, y))):
                continue
            stack = [(x, y)]
            seen.add((x, y))
            pts = []
            while stack:
                cx, cy = stack.pop()
                pts.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if x0 <= nx < x1 and y0 <= ny < y1 and (nx, ny) not in seen and is_ink(im.getpixel((nx, ny))):
                        seen.add((nx, ny))
                        stack.append((nx, ny))
            comps.append(pts)
    return comps


def read_fnt(path):
    """Lee los glifos de un .fnt binario de BMFont: {código: (x, y, w, h, xoff, yoff)}."""
    data = open(path, 'rb').read()
    assert data[:3] == b'BMF'
    pos = 4
    chars = {}
    while pos < len(data):
        btype = data[pos]
        size = struct.unpack_from('<I', data, pos + 1)[0]
        body = pos + 5
        if btype == 4:
            for i in range(size // 20):
                cid, x, y, w, h, xo, yo, xa, page, ch = struct.unpack_from('<IHHHHhhhBB', data, body + i * 20)
                chars[cid] = (x, y, w, h, xo, yo)
        pos = body + size
    return chars


def glyph_mask(font_png, chars, ch, height):
    x, y, w, h, _, _ = chars[ord(ch)]
    g = font_png.crop((x, y, x + w, y + h)).getchannel('A')
    # Recorta filas/columnas vacías y escala al alto pedido (pixel art: vecino más cercano).
    bbox = g.getbbox()
    g = g.crop(bbox)
    scale = height / g.height
    return g.resize((max(1, round(g.width * scale)), height), Image.NEAREST).point(lambda a: 255 if a > 100 else 0)


def make_page_sheet(sheet, font_png, chars, digits, ink):
    out = sheet.copy()
    for box, digit in zip(LABELS, digits):
        comps = components(sheet, box)
        # El número es la última componente por la derecha que no sea el borde de la tarjeta.
        comps = [c for c in comps if len(c) > 8]
        comps.sort(key=lambda c: max(p[0] for p in c))
        num = comps[-1]
        xs = [p[0] for p in num]
        ys = [p[1] for p in num]
        bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
        # Borra el número (y sus bordes suavizados) pintando cada píxel con el papel de su izquierda.
        paper_sum = sum(sheet.getpixel((bx0 - 3, (by0 + by1) // 2))[:3])
        for py in range(by0 - 1, by1 + 2):
            fill = None
            for dx in range(2, 30):
                c = sheet.getpixel((bx0 - dx, py))
                if sum(c[:3]) >= paper_sum - 40 and c[3] == 255:
                    fill = c
                    break
            for px in range(bx0 - 1, bx1 + 2):
                c = sheet.getpixel((px, py))
                if fill and c[3] == 255 and sum(c[:3]) < paper_sum - 40:
                    out.putpixel((px, py), fill)
        # Dibuja el nuevo número con la letra del juego, del alto del original.
        height = by1 - by0 + 1
        mask = glyph_mask(font_png, chars, digit, height)
        ox = (bx0 + bx1 + 1) // 2 - mask.width // 2
        layer = Image.new('RGBA', mask.size, ink)
        out.paste(layer, (ox, by0), mask)
    return out


def most_common_ink(sheet):
    counts = {}
    for y in range(408, 443):
        for x in range(20, 440):
            px = sheet.getpixel((x, y))
            if is_ink(px):
                counts[px] = counts.get(px, 0) + 1
    return max(counts, key=counts.get)


def make_tabs(sheet, font_png, chars, ink):
    """Pestañas de papel para los bordes: ◀, ▶ y +, normal y seleccionada; puntos de página."""
    paper = sheet.getpixel((80, 300))
    paper_sel = (min(255, paper[0] + 12), min(255, paper[1] + 12), min(255, paper[2] + 12), 255)
    edge = (110, 96, 96, 255)
    cells = []
    W, H = 34, 40
    atlas = Image.new('RGBA', (W * 6, H + 16), (0, 0, 0, 0))

    def tab(ix, symbol, selected):
        im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        # Papel con bordes irregulares, como las pestañas del menú ("TAB MODS").
        poly = [(3, 2), (W - 4, 4), (W - 2, H // 2), (W - 3, H - 3), (4, H - 2), (1, H // 2 + 3)]
        d.polygon([(x + 2, y + 2) for x, y in poly], fill=(0, 0, 0, 70))  # sombra
        d.polygon(poly, fill=paper_sel if selected else paper, outline=edge)
        d.line(poly + [poly[0]], fill=edge, width=2)
        if symbol == '+':
            c = (W // 2, H // 2)
            d.rectangle((c[0] - 2, c[1] - 9, c[0] + 1, c[1] + 9), fill=ink)
            d.rectangle((c[0] - 9, c[1] - 2, c[0] + 9, c[1] + 1), fill=ink)
        else:
            cx, cy = W // 2, H // 2
            if symbol == '>':
                tri = [(cx - 6, cy - 10), (cx + 8, cy), (cx - 6, cy + 10)]
            else:
                tri = [(cx + 6, cy - 10), (cx - 8, cy), (cx + 6, cy + 10)]
            d.polygon(tri, fill=ink)
        if selected:
            d.line(poly + [poly[0]], fill=ink, width=2)
        atlas.paste(im, (ix * W, 0))
        return (ix * W, 0, W, H)

    frames = {
        'Prev': tab(0, '<', False), 'PrevSel': tab(1, '<', True),
        'Next': tab(2, '>', False), 'NextSel': tab(3, '>', True),
        'Plus': tab(4, '+', False), 'PlusSel': tab(5, '+', True),
    }
    # Puntos de página (vacío y lleno), 10x10.
    d = ImageDraw.Draw(atlas)
    d.ellipse((1, H + 3, 10, H + 12), outline=ink, width=2)
    d.ellipse((15, H + 3, 24, H + 12), fill=ink)
    frames['Dot'] = (0, H + 2, 12, 12)
    frames['DotOn'] = (14, H + 2, 12, 12)
    return atlas, frames


def write_anm2(path, png_name, frames):
    anims = []
    for name, (x, y, w, h) in frames.items():
        anims.append(
            f'    <Animation Name="{name}" FrameNum="1" Loop="true">\n'
            f'      <RootAnimation><Frame XPosition="0" YPosition="0" XPivot="0" YPivot="0" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/></RootAnimation>\n'
            f'      <LayerAnimations><LayerAnimation LayerId="0" Visible="true">'
            f'<Frame XPosition="0" YPosition="0" XPivot="{w // 2}" YPivot="{h // 2}" XCrop="{x}" YCrop="{y}" Width="{w}" Height="{h}" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/>'
            f'</LayerAnimation></LayerAnimations>\n'
            f'      <NullAnimations/><Triggers/>\n'
            f'    </Animation>')
    xml = (
        '<AnimatedActor>\n'
        '  <Info CreatedBy="More Save Slots" Version="1" Fps="30"/>\n'
        '  <Content>\n'
        f'    <Spritesheets><Spritesheet Path="{png_name}" Id="0"/></Spritesheets>\n'
        '    <Layers><Layer Name="Main" Id="0" SpritesheetId="0"/></Layers>\n'
        '    <Nulls/><Events/>\n'
        '  </Content>\n'
        f'  <Animations DefaultAnimation="Plus">\n' + '\n'.join(anims) + '\n  </Animations>\n'
        '</AnimatedActor>\n')
    open(path, 'w', encoding='utf-8').write(xml)


sheet = Image.open(SHEET).convert('RGBA')
font_png = Image.open(FONT_PNG).convert('RGBA')
chars = read_fnt(FONT_FNT)
ink = most_common_ink(sheet)
print('tinta', ink)
make_page_sheet(sheet, font_png, chars, '456', ink).save(os.path.join(OUT, 'saveselectmenu_p2.png'))
make_page_sheet(sheet, font_png, chars, '789', ink).save(os.path.join(OUT, 'saveselectmenu_p3.png'))
atlas, frames = make_tabs(sheet, font_png, chars, ink)
atlas.save(os.path.join(OUT, 'more_save_slots.png'))
write_anm2(os.path.join(OUT, 'more_save_slots.anm2'), 'more_save_slots.png', frames)
print('listo:', OUT)
