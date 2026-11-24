"""Hammie in his original drawing (the Kode Dot sticker), cut into parts and animated.

Source: out/reference.png, the sticker as shown on the Kickstarter page (Kode's artwork). The
drawing is cut out along its black outline, split into body, head, pliers arm and scope arm, and
each frame puts them back with small moves (breathing, head tilt, arm swing). Eyes and the scope's
screen are repainted per frame. A white sticker border goes round the result, on black, the way
the Dot / Kode Watch show him.

    hamorig.py OUTDIR [--fps 15] [--gif]
writes OUTDIR/<role>/NNN.png (410 x 334) for the watch's six roles, plus a contact sheet.
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.environ.get("HAMMIE_REF", os.path.join(HERE, "reference.png"))   # the sticker image: not in this repository

OUT_W, OUT_H = 410, 334          # the watch's Kody box
SS = 2                            # supersampling
MARGIN_T, MARGIN_X, MARGIN_B = 120, 60, 12   # room around the drawing (ref pixels) for props
INK = (27, 27, 27)

# ---- the parts, in reference pixels (996 x 956)
HEAD = [(262, 0), (720, 0), (760, 230), (756, 340), (700, 392), (598, 384), (590, 440), (560, 462), (470, 486),
        (380, 488), (300, 470), (236, 430), (186, 362), (176, 330), (176, 160), (215, 120)]
HEAD_PIVOT = (430, 480)
PLIERS = [(10, 120), (175, 120), (186, 300), (160, 430), (205, 445), (228, 470), (230, 560), (190, 600),
          (140, 650), (60, 650), (25, 560)]
PLIERS_PIVOT = (220, 520)
SCOPE = [(596, 372), (965, 372), (965, 705), (680, 712), (628, 680), (596, 640)]
SCOPE_PIVOT = (640, 650)
EYES = [((318, 262), (24, 27)), ((488, 258), (26, 29))]
LENS = (246, 130, 33)
SMILE_BOX = (296, 352, 470, 440)
LOOP_SEEDS = [(180, 720)]


def poly_mask(size, pts, blur=0):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return m.filter(ImageFilter.GaussianBlur(blur)) if blur else m


def cutout():
    im = Image.open(REF).convert("RGB")
    W, H = im.size
    dark = im.convert("L").point(lambda v: 255 if v < 110 else 0)
    m = Image.new("L", (W, H), 255)
    m.paste(0, mask=dark)
    for x in range(0, W, 7):
        for y in (0, H - 1):
            if m.getpixel((x, y)) == 255:
                ImageDraw.floodfill(m, (x, y), 128, thresh=0)
    for y in range(0, H, 7):
        for x in (0, W - 1):
            if m.getpixel((x, y)) == 255:
                ImageDraw.floodfill(m, (x, y), 128, thresh=0)
    alpha = m.point(lambda v: 0 if v == 128 else 255).filter(ImageFilter.MaxFilter(3))
    # the white inside the cable's loop (lower left) is background, not Hammie
    wh = im.convert("L").point(lambda v: 255 if v > 200 else 0)
    for seed in LOOP_SEEDS:
        if wh.getpixel(seed) == 255:
            ImageDraw.floodfill(wh, seed, 128, thresh=0)
    hole = wh.point(lambda v: 255 if v == 128 else 0).filter(ImageFilter.MaxFilter(5))
    alpha = ImageChops.subtract(alpha, hole)
    rgba = im.convert("RGBA")
    rgba.putalpha(alpha)
    return rgba


class Parts:
    def __init__(self):
        src = cutout()
        self.size = src.size
        W, H = src.size
        hm = poly_mask(src.size, HEAD)
        pm = poly_mask(src.size, PLIERS)
        sm = poly_mask(src.size, SCOPE)

        def take(mask):
            p = src.copy()
            p.putalpha(ImageChops.multiply(src.getchannel("A"), mask))
            return p

        self.head = take(hm)
        self.pliers = take(pm)
        self.scope = take(sm)
        # the body: everything else; where the head was, a neck of fur so a tilt shows no hole
        body = src.copy()
        a = body.getchannel("A")
        cut = ImageChops.lighter(ImageChops.lighter(hm, pm), sm)
        a = ImageChops.subtract(a, cut)
        neck = Image.new("RGBA", src.size, (0, 0, 0, 0))
        nd = ImageDraw.Draw(neck)
        nd.polygon([(250, 330), (700, 330), (700, 470), (600, 470), (560, 500), (300, 500), (250, 440)], fill=(242, 128, 30, 255))
        nd.polygon([(300, 380), (560, 380), (580, 470), (540, 505), (320, 505), (290, 450)], fill=(250, 250, 248, 255))
        neck.putalpha(ImageChops.multiply(neck.getchannel("A"), hm.filter(ImageFilter.MinFilter(31))))
        body.putalpha(a)
        # behind the arms: the torso's fur, so a swing shows no hole
        torso = [(232, 470), (300, 450), (560, 470), (610, 450), (700, 560), (720, 700), (700, 860), (200, 860), (140, 700), (200, 560)]
        tm = ImageChops.multiply(poly_mask(src.size, torso), ImageChops.lighter(pm, sm))
        fur = Image.new("RGBA", src.size, (242, 128, 30, 255))
        fur.putalpha(tm.filter(ImageFilter.MinFilter(9)))
        neck = Image.alpha_composite(neck, fur)
        self.body = Image.alpha_composite(neck, body)
        # the lens colour round each eye, to paint a shut eye over
        px = src.load()
        self.lens = []
        for (cx, cy), (rx, ry) in EYES:
            acc, n = [0, 0, 0], 0
            for ang in range(0, 360, 10):
                for rr in (1.35, 1.5):
                    x = int(cx + rx * rr * math.cos(math.radians(ang)))
                    y = int(cy + ry * rr * math.sin(math.radians(ang)))
                    r, g, b, al = px[x, y]
                    if al and r > 180 and 60 < g < 190 and b < 90:
                        acc[0] += r; acc[1] += g; acc[2] += b; n += 1
            self.lens.append(tuple(v // max(n, 1) for v in acc) if n else LENS)
        # where the scope arm was over the body: body fur behind it
        self.screen_mask = self._screen_mask(src)

    def _screen_mask(self, src):
        px = src.load()
        m = Image.new("L", src.size, 0)
        mp = m.load()
        for y in range(380, 600):
            for x in range(620, 900):
                r, g, b, a = px[x, y]
                if a and g > r + 25 and g > b + 15:
                    mp[x, y] = 255
        m = m.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
        return m


P = None


def parts():
    global P
    if P is None:
        P = Parts()
    return P


# ---------------------------------------------------------------- repaints
def paint_eyes(head, mode, look=(0, 0), wide=1.0):
    """mode: open | shut | happy | blink(0..1 closed)."""
    d = ImageDraw.Draw(head)
    for ei, ((cx, cy), (rx, ry)) in enumerate(EYES):
        if mode == "open" and wide == 1.0 and look == (0, 0):
            continue
        # clear the pupil to the lens colour around it
        d.ellipse([cx - rx - 3, cy - ry - 3, cx + rx + 3, cy + ry + 3], fill=parts().lens[ei] + (255,))
        if mode == "open":
            ex, ey = cx + look[0], cy + look[1]
            r1, r2 = rx * wide, ry * wide
            d.ellipse([ex - r1, ey - r2, ex + r1, ey + r2], fill=INK + (255,))
            s = 7 * wide
            d.ellipse([ex + r1 * 0.15 - s, ey - r2 * 0.35 - s, ex + r1 * 0.15 + s, ey - r2 * 0.35 + s], fill=(255, 255, 255, 255))
        elif mode == "shut":
            d.arc([cx - rx - 4, cy - ry, cx + rx + 4, cy + ry + 6], 20, 160, fill=INK + (255,), width=9)
        elif mode == "happy":
            d.arc([cx - rx - 4, cy - 6, cx + rx + 4, cy + ry * 2], 200, 340, fill=INK + (255,), width=9)


def paint_mouth(head, mode):
    if mode == "smile":
        return
    d = ImageDraw.Draw(head)
    x0, y0, x1, y1 = SMILE_BOX
    if mode == "open":
        # open, laughing mouth under the nose
        d.chord([318, 360, 445, 452], 0, 180, fill=(110, 30, 38, 255), outline=INK + (255,), width=8)
        d.ellipse([352, 412, 410, 446], fill=(240, 120, 110, 255))
    elif mode == "o":
        d.ellipse([360, 378, 404, 428], fill=(110, 30, 38, 255), outline=INK + (255,), width=7)


SCR_G = (46, 125, 50)
LIME = (182, 232, 72)


def paint_screen(scope, mode, t):
    p = parts()
    m = p.screen_mask
    bbox = m.getbbox()
    layer = Image.new("RGBA", scope.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x0, y0, x1, y1 = bbox
    if mode == "err":
        d.rectangle(bbox, fill=(220, 50, 45, 255))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        d.line([cx - 40, cy - 40, cx + 40, cy + 40], fill=(255, 255, 255, 255), width=16)
        d.line([cx + 40, cy - 40, cx - 40, cy + 40], fill=(255, 255, 255, 255), width=16)
    else:
        d.rectangle(bbox, fill=SCR_G + (255,))
        if mode == "clock":
            cx, cy, r = (x0 + x1) / 2, (y0 + y1) / 2, 52
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(250, 250, 245, 255), outline=INK + (255,), width=7)
            a_m = t * 2 * math.pi * 1.0
            a_h = t * 2 * math.pi / 12
            d.line([cx, cy, cx + 40 * math.sin(a_m), cy - 40 * math.cos(a_m)], fill=INK + (255,), width=7)
            d.line([cx, cy, cx + 26 * math.sin(a_h), cy - 26 * math.cos(a_h)], fill=INK + (255,), width=9)
        else:
            pts = []
            for k in range(60):
                u = k / 59
                x = x0 + 22 + u * (x1 - x0 - 44)
                # the trace leans with the screen (the drawing's screen is turned a little)
                y = (y0 + y1) / 2 + 6 * (u - 0.5) * 2 - 34 * math.sin(2 * math.pi * (u * 1.6 - t))
                pts.append((x, y))
            d.line(pts, fill=LIME + (255,), width=8, joint="curve")
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), m))
    return Image.alpha_composite(scope, layer)


# ---------------------------------------------------------------- props (drawn in the sticker's style)
def outline_poly(d, pts, fill, w=9):
    d.polygon(pts, fill=fill)
    d.line(pts + [pts[0]], fill=INK, width=w, joint="curve")


def prop_bang(d, x, y, s):
    w = 22 * s
    d.rounded_rectangle([x - w, y - 95 * s, x + w, y + 10 * s], radius=w, fill=(255, 194, 26), outline=INK, width=int(9 * s) or 1)
    d.ellipse([x - w, y + 25 * s, x + w, y + 25 * s + 2 * w], fill=(255, 194, 26), outline=INK, width=int(9 * s) or 1)


def prop_bolt(d, x, y, s):
    pts = [(14, -70), (-30, 6), (2, 6), (-12, 64), (36, -14), (4, -14)]
    outline_poly(d, [(x + px * s, y + py * s) for px, py in pts], (255, 194, 26), int(9 * s))


def prop_battery(d, x, y, fill):
    d.rounded_rectangle([x, y, x + 120, y + 60], radius=12, fill=(34, 40, 49), outline=INK, width=9)
    d.rounded_rectangle([x + 120, y + 16, x + 134, y + 44], radius=4, fill=(34, 40, 49), outline=INK, width=6)
    if fill > 0:
        d.rounded_rectangle([x + 12, y + 12, x + 12 + 96 * fill, y + 48], radius=6, fill=(94, 224, 138))


def prop_z(d, x, y, s, col=(154, 209, 255)):
    pts = [(x, y), (x + 40 * s, y), (x, y + 44 * s), (x + 40 * s, y + 44 * s)]
    d.line(pts, fill=INK, width=int(18 * s), joint="curve")
    d.line(pts, fill=col, width=int(9 * s), joint="curve")


# ---------------------------------------------------------------- one frame
def rot_layer(layer, deg, pivot, dx=0, dy=0, scale=1.0):
    if not deg and not dx and not dy and scale == 1.0:
        return layer
    a = math.radians(deg)
    cx, cy = pivot
    # inverse affine for PIL: output (x,y) -> input
    ca, sa = math.cos(a) / scale, math.sin(a) / scale
    x0, y0 = cx + dx, cy + dy
    coeffs = (ca, sa, cx - ca * x0 - sa * y0, -sa, ca, cy + sa * x0 - ca * y0)
    return layer.transform(layer.size, Image.AFFINE, coeffs, resample=Image.BICUBIC)


def frame(pose):
    """pose: dict with head_r, head_dy, head_dx, pliers_r, scope_r, breathe (0..1), hop (px up), squash,
    eyes, look, wide, mouth, screen, t, props (list of callables d->None in ref coords incl. margins)."""
    p = parts()
    W, H = p.size
    br = pose.get("breathe", 0.0)
    lift = 6 * br                       # chest rises
    head = p.head.copy()
    paint_eyes(head, pose.get("eyes", "open"), pose.get("look", (0, 0)), pose.get("wide", 1.0))
    paint_mouth(head, pose.get("mouth", "smile"))
    scope = paint_screen(p.scope.copy(), pose.get("screen", "wave"), pose.get("t", 0.0))

    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    body = p.body
    if br:
        body = body.resize((W, int(H * (1 + 0.012 * br))), Image.BICUBIC)
        body = body.crop((0, body.height - H, W, body.height))
    canvas.alpha_composite(body)
    canvas.alpha_composite(rot_layer(head, pose.get("head_r", 0), HEAD_PIVOT, pose.get("head_dx", 0), pose.get("head_dy", 0) - lift))
    canvas.alpha_composite(rot_layer(scope, pose.get("scope_r", 0), SCOPE_PIVOT, 0, -lift * 0.6 + pose.get("scope_dy", 0)))
    canvas.alpha_composite(rot_layer(p.pliers, pose.get("pliers_r", 0), PLIERS_PIVOT, 0, -lift * 0.6 + pose.get("pliers_dy", 0)))

    # whole-body hop / squash about the feet
    sq = pose.get("squash", 0.0)
    hop = pose.get("hop", 0.0)
    if sq:
        nw, nh = int(W * (1 + sq)), int(H * (1 - sq))
        c2 = canvas.resize((nw, nh), Image.BICUBIC)
        canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        canvas.alpha_composite(c2, ((W - nw) // 2, H - nh))

    # onto the stage, with margins for props
    SW, SH = W + 2 * MARGIN_X, H + MARGIN_T + MARGIN_B
    stage = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
    stage.alpha_composite(canvas, (MARGIN_X, MARGIN_T - int(hop)))
    # sticker border round Hammie (and props drawn with it)
    pd = ImageDraw.Draw(stage)
    for fn in pose.get("props", []):
        fn(pd)
    # down to 2x output
    sc = (OUT_H * SS) / SH
    big = stage.resize((int(SW * sc), OUT_H * SS), Image.LANCZOS)
    out2 = Image.new("RGBA", (OUT_W * SS, OUT_H * SS), (0, 0, 0, 0))
    out2.alpha_composite(big, ((OUT_W * SS - big.width) // 2, 0))
    a = out2.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    # thin gaps a moved part opens inside the outline: fur, not sticker white
    closed = a.filter(ImageFilter.MaxFilter(19)).filter(ImageFilter.MinFilter(19))
    gap = ImageChops.subtract(closed, a)
    if gap.getbbox():
        fur = Image.new("RGBA", out2.size, (238, 122, 28, 255))
        fur.putalpha(gap)
        fur.alpha_composite(out2)
        out2 = fur
        a = out2.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    border = a.filter(ImageFilter.MaxFilter(17)).filter(ImageFilter.GaussianBlur(1.2))
    bg = Image.new("RGBA", out2.size, (0, 0, 0, 255))
    white = Image.new("RGBA", out2.size, (255, 255, 255, 255))
    bg.paste(white, mask=border)
    bg.alpha_composite(out2)
    out = bg.resize((OUT_W, OUT_H), Image.LANCZOS).convert("RGB")
    # the source is a soft web picture: sharpen at the size it is shown
    return out.filter(ImageFilter.UnsharpMask(radius=1.2, percent=110, threshold=2))


# ---------------------------------------------------------------- the six watch roles
def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def blink_eyes(t, at, dur=0.1):
    return "shut" if at <= t < at + dur else "open"


def k(t, keys):
    """keys [(t, v)] eased between."""
    if t <= keys[0][0]:
        return keys[0][1]
    for (a, va), (b, vb) in zip(keys, keys[1:]):
        if a <= t <= b:
            return va + (vb - va) * ease((t - a) / (b - a))
    return keys[-1][1]


def role_idle(n):
    for i in range(n):
        t = i / n
        s = math.sin(2 * math.pi * t)
        yield {"breathe": (1 - math.cos(2 * math.pi * t)) / 2, "head_r": 1.5 * s, "pliers_r": -2 * s, "scope_r": 1.5 * s,
               "eyes": blink_eyes(t, 0.66, 0.07), "t": t * 3}


def role_notice(n):
    for i in range(n):
        t = i / n
        pop = k(t, [(0, 0), (0.08, 1.25), (0.16, 1.0), (0.85, 1.0), (1.0, 0)])
        yield {"hop": k(t, [(0, 0), (0.08, 40), (0.18, 0), (1, 0)]), "wide": k(t, [(0, 1), (0.1, 1.25), (0.8, 1.25), (0.95, 1)]),
               "mouth": "o" if 0.06 < t < 0.4 else "smile", "scope_r": k(t, [(0, 0), (0.15, -8), (0.8, -8), (1, 0)]),
               "t": t * 2, "props": [lambda d, s=pop: prop_bang(d, MARGIN_X + 470, 110, s) if s > 0.02 else None]}


def role_charge(n):
    for i in range(n):
        t = i / n
        pulse = 1 + 0.12 * math.sin(4 * math.pi * t)
        fill = min(1.0, t * 1.15)
        yield {"breathe": (1 - math.cos(4 * math.pi * t)) / 2, "eyes": "happy", "t": t * 2,
               "props": [lambda d, s=pulse: prop_bolt(d, MARGIN_X - 10, 150, s),
                         lambda d, f=fill: prop_battery(d, MARGIN_X + 830, 20, f)]}


def role_look(n):
    for i in range(n):
        t = i / n
        yield {"head_r": k(t, [(0, 0), (0.2, -4), (0.8, -4), (1, 0)]), "wide": k(t, [(0, 1), (0.2, 1.22), (0.8, 1.22), (1, 1)]),
               "eyes": blink_eyes(t, 0.5, 0.08), "pliers_r": k(t, [(0, 0), (0.15, 10), (0.3, 0)]), "t": t * 2}


def role_hour(n):
    for i in range(n):
        t = i / n
        on = 0.12 < t < 0.85
        yield {"head_r": k(t, [(0, 0), (0.15, 5), (0.85, 5), (1, 0)]), "look": (int(k(t, [(0, 0), (0.15, 9), (0.85, 9), (1, 0)])), 6 if on else 0),
               "scope_r": k(t, [(0, 0), (0.15, -7), (0.85, -7), (1, 0)]), "screen": "clock" if on else "wave",
               "t": (t - 0.12) / 0.73 if on else t * 2}


def role_wake(n):
    for i in range(n):
        t = i / n
        shut = t < 0.45
        yield {"eyes": "shut" if shut else blink_eyes(t, 0.8, 0.06), "mouth": "o" if 0.12 < t < 0.4 else "smile",
               "head_r": k(t, [(0, 7), (0.3, 7), (0.6, -3), (1, 0)]), "head_dy": k(t, [(0, 14), (0.3, 14), (0.6, -4), (1, 0)]),
               "pliers_r": k(t, [(0, -8), (0.35, -8), (0.55, 14), (0.75, 14), (1, 0)]),
               "scope_r": k(t, [(0, 6), (0.35, 6), (0.55, -10), (0.75, -10), (1, 0)]),
               "breathe": k(t, [(0, 0), (0.45, 0), (0.6, 1.6), (0.8, 1.6), (1, 0)]), "t": t * 2}


ROLES = [("idle", role_idle, 3.0), ("notice", role_notice, 2.0), ("charge", role_charge, 2.0),
         ("look", role_look, 2.0), ("hour", role_hour, 2.0), ("wake", role_wake, 2.0)]


def main():
    out = sys.argv[1]
    fps = 15
    if "--fps" in sys.argv:
        fps = int(sys.argv[sys.argv.index("--fps") + 1])
    only = [a for a in sys.argv[2:] if not a.startswith("--") and not a.isdigit()]
    sheet = []
    for name, fn, secs in ROLES:
        if only and name not in only:
            continue
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        n = int(secs * fps)
        frs = []
        for i, pose in enumerate(fn(n)):
            pose["props"] = [f for f in pose.get("props", []) if f]
            im = frame(pose)
            im.save(os.path.join(d, "%03d.png" % i))
            frs.append(im)
        sheet.append(frs[:: max(1, len(frs) // 6)][:6])
        frs[0].save(os.path.join(out, name + ".gif"), save_all=True, append_images=frs[1:], duration=int(1000 / fps), loop=0)
        print(name, n, "frames")
    cw, ch = OUT_W // 2, OUT_H // 2
    s = Image.new("RGB", (cw * 6, ch * len(sheet)))
    for r, frs in enumerate(sheet):
        for c, f in enumerate(frs):
            s.paste(f.resize((cw, ch)), (c * cw, r * ch))
    s.save(os.path.join(out, "sheet.png"))


if __name__ == "__main__":
    main()
