# Conceptos de icono para el instalador de More Save Slots. Todo dibujado aquí (sin sprites del juego).
# Uso: python -I iconos.py <carpeta_salida>
# Deja <id>_<tamaño>.png (256, 48, 32, 16) y <id>.ico por concepto.
import sys
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

SALIDA = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(SALIDA, exist_ok=True)

S = 1024  # se dibuja grande y se reduce: bordes suaves
PAPEL = (237, 227, 207, 255)
PAPEL_SOMBRA = (205, 191, 164, 255)
TINTA = (43, 29, 20, 255)
ROJO = (200, 50, 43, 255)
VERDE = (64, 168, 74, 255)
TAMANOS = [256, 48, 32, 16]

FUENTES = ["C:/Windows/Fonts/seguibl.ttf", "C:/Windows/Fonts/ariblk.ttf", "C:/Windows/Fonts/arialbd.ttf"]


def fuente(px):
    for f in FUENTES:
        if os.path.exists(f):
            return ImageFont.truetype(f, px)
    return ImageFont.load_default()


def tarjeta(w, h, borde, contenido=None, papel=PAPEL):
    """Una ficha de papel con borde de tinta y una franja de sombra abajo (como una ficha de partida)."""
    img = Image.new("RGBA", (w + borde * 2, h + borde * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(min(w, h) * 0.09)
    d.rounded_rectangle([borde // 2, borde // 2, w + borde * 1.5, h + borde * 1.5], r, fill=papel, outline=TINTA, width=borde)
    # sombra interior inferior (papel doblado)
    d.rounded_rectangle([borde * 1.6, h * 0.80, w + borde * 0.4, h + borde * 0.4], int(r * 0.7), fill=PAPEL_SOMBRA)
    if contenido:
        contenido(d, img.size)
    return img


def pegar_rotada(base, capa, centro, angulo):
    rot = capa.rotate(angulo, resample=Image.BICUBIC, expand=True)
    x = int(centro[0] - rot.width / 2)
    y = int(centro[1] - rot.height / 2)
    base.alpha_composite(rot, (x, y))


def mas(d, cx, cy, tam, grosor, color, contorno=None):
    """Un «+» hecho con dos barras (con contorno opcional para que se lea sobre cualquier fondo)."""
    g = grosor / 2
    if contorno:
        c, gc = contorno
        d.rounded_rectangle([cx - tam / 2 - gc, cy - g - gc, cx + tam / 2 + gc, cy + g + gc], g + gc, fill=c)
        d.rounded_rectangle([cx - g - gc, cy - tam / 2 - gc, cx + g + gc, cy + tam / 2 + gc], g + gc, fill=c)
    d.rounded_rectangle([cx - tam / 2, cy - g, cx + tam / 2, cy + g], g, fill=color)
    d.rounded_rectangle([cx - g, cy - tam / 2, cx + g, cy + tam / 2], g, fill=color)


def sombra(img, desp=18, radio=22, alfa=110):
    """Sombra suave bajo todo el dibujo."""
    a = img.split()[3].point(lambda v: v * alfa // 255)
    s = Image.new("RGBA", img.size, (0, 0, 0, 0))
    s.putalpha(a)
    s = s.filter(ImageFilter.GaussianBlur(radio))
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.alpha_composite(s, (desp // 2, desp))
    out.alpha_composite(img)
    return out


# ---------- A: fichas en abanico con «+» ----------
def concepto_a():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w, h, b = 470, 620, 34
    pegar_rotada(img, tarjeta(w, h, b), (390, 540), 16)
    pegar_rotada(img, tarjeta(w, h, b), (512, 512), 3)

    def frente(d, size):
        cx, cy = size[0] / 2, size[1] * 0.43
        mas(d, cx, cy, 300, 92, VERDE)
    pegar_rotada(img, tarjeta(w, h, b, frente), (630, 520), -10)
    return sombra(img)


# ---------- B: ficha con un 9 grande y un «+» de insignia ----------
def concepto_b():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w, h, b = 560, 720, 36
    pegar_rotada(img, tarjeta(w, h, b), (440, 500), 9)

    def frente(d, size):
        f = fuente(560)
        t = "9"
        caja = d.textbbox((0, 0), t, font=f)
        tw, th = caja[2] - caja[0], caja[3] - caja[1]
        d.text(((size[0] - tw) / 2 - caja[0], size[1] * 0.40 - th / 2 - caja[1]), t, font=f, fill=TINTA)
    pegar_rotada(img, tarjeta(w, h, b, frente), (540, 530), -4)
    d = ImageDraw.Draw(img)
    # insignia redonda con «+»
    cx, cy, r = 800, 230, 175
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=ROJO, outline=TINTA, width=34)
    mas(d, cx, cy, 190, 62, PAPEL)
    return sombra(img)


# ---------- C: dos páginas que se deslizan (la idea del mod) ----------
def concepto_c():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w, h, b = 400, 540, 32

    def lineas(d, size):
        # dos «renglones» de tinta: una ficha con algo escrito
        for i, y in enumerate((0.22, 0.34)):
            d.rounded_rectangle([size[0] * 0.22, size[1] * y, size[0] * (0.78 if i == 0 else 0.6), size[1] * y + 28], 14, fill=TINTA)

    pegar_rotada(img, tarjeta(w, h, b, lineas, papel=PAPEL_SOMBRA), (262, 450), -7)

    def frente(d, size):
        lineas(d, size)
        mas(d, size[0] / 2, size[1] * 0.58, 200, 64, VERDE)
    pegar_rotada(img, tarjeta(w, h, b, frente), (690, 470), 4)
    d = ImageDraw.Draw(img)
    # flecha de deslizar
    y = 860
    d.rounded_rectangle([250, y - 30, 690, y + 30], 30, fill=TINTA)
    d.polygon([(680, y - 95), (820, y), (680, y + 95)], fill=TINTA)
    return sombra(img)


# ---------- D: pixel art (como los gráficos del juego, pero propio) ----------
def concepto_d():
    pi = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(pi)
    def caja(x0, y0, x1, y1, relleno):
        d.rectangle([x0, y0, x1, y1], fill=TINTA)
        d.rectangle([x0 + 1, y0 + 1, x1 - 1, y1 - 1], fill=relleno)
    # dos fichas detrás (solo asoma la parte de arriba) y la de delante
    caja(8, 1, 27, 12, PAPEL_SOMBRA)
    caja(5, 4, 27, 15, PAPEL_SOMBRA)
    caja(2, 7, 27, 30, PAPEL)
    d.rectangle([3, 26, 26, 29], fill=PAPEL_SOMBRA)       # franja de sombra abajo
    d.rectangle([4, 9, 7, 9], fill=(255, 250, 238, 255))   # brillo
    d.rectangle([4, 9, 4, 11], fill=(255, 250, 238, 255))
    # «+» verde con contorno de tinta
    d.rectangle([11, 11, 18, 24], fill=TINTA)
    d.rectangle([7, 14, 22, 21], fill=TINTA)
    d.rectangle([12, 12, 17, 23], fill=VERDE)
    d.rectangle([8, 15, 21, 20], fill=VERDE)
    lienzo = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    r = pi.crop(pi.getbbox())
    lienzo.alpha_composite(r, ((32 - r.width) // 2, (32 - r.height) // 2))
    return lienzo  # se escala con vecino más próximo


CONCEPTOS = {
    "a_fichas": (concepto_a, False),
    "b_nueve": (concepto_b, False),
    "c_deslizar": (concepto_c, False),
    "d_pixel": (concepto_d, True),
}

for nombre, (dibujar, pixel) in CONCEPTOS.items():
    base = dibujar()
    imgs = []
    for t in TAMANOS:
        if pixel:
            # múltiplos enteros cuando se puede; 48 = 32 escalado ×1.5 con vecino más próximo
            im = base.resize((t, t), Image.NEAREST) if t != 16 else base.resize((16, 16), Image.BOX)
        else:
            im = base.resize((t, t), Image.LANCZOS)
        im.save(os.path.join(SALIDA, f"{nombre}_{t}.png"))
        imgs.append(im)
    imgs[0].save(os.path.join(SALIDA, f"{nombre}.ico"), sizes=[(t, t) for t in TAMANOS], append_images=imgs[1:])
    print("ok", nombre)
