"""Sprites for Kody vs Hammie: every pose of both fighters, as RGB565 C arrays.

Hammie is put together from the four parts hamorig.py cuts out of his sticker (body, head, pliers
arm, scope arm), turned and moved per pose. Kody is drawn here in the style of his animations: a
round blue hamster with two teeth. Both face the viewer, a little turned to the right; the game
mirrors a fighter that faces left.

    gen_sprites.py OUT_DIR [--ref sticker.png]
writes OUT_DIR/sprites_gen.h and OUT_DIR/sprites_preview.png
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

SPR_W, SPR_H = 128, 84          # one frame, in game pixels (half the glass)
FOOT_Y = 80                    # the feet's row in the frame
KEY = 0xF81F                   # transparent

POSES = ["idle0", "idle1", "walk0", "walk1", "punch", "kick", "block", "crouch", "jump", "hit", "ko",
         "win", "special"]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


# ---------------------------------------------------------------- Hammie, from his sticker
def hammie_frames():
    import hamorig as H
    P = H.parts()
    W, Hh = P.size                                    # 996 x 956, feet at the bottom
    MX, MT = 260, 200                                 # room round him for the arms' swings
    SW, SH = W + 2 * MX, Hh + MT + 20
    poses = {
        "idle0": {"breathe": 0.0},
        "idle1": {"breathe": 1.0, "head_r": 1.5},
        "walk0": {"hop": 18, "head_r": -3, "pliers_r": -6, "scope_r": 4},
        "walk1": {"hop": 0, "head_r": 3, "pliers_r": 6, "scope_r": -4, "squash": 0.03},
        "punch": {"scope_r": -18, "scope_dx": 170, "scope_dy": -40, "head_r": -4, "head_dx": 25, "wide": 1.2, "mouth": "o"},
        "kick": {"pliers_r": 125, "pliers_dx": 120, "pliers_dy": 60, "head_r": -6, "head_dx": 20, "eyes": "happy"},
        "block": {"scope_r": -28, "scope_dx": 40, "scope_dy": -150, "head_r": 5, "head_dy": 20, "eyes": "shut", "squash": 0.04},
        "crouch": {"squash": 0.22, "head_dy": 10, "pliers_r": -15, "scope_r": 10},
        "jump": {"pliers_r": -25, "scope_r": 20, "scope_dy": -40, "eyes": "happy", "squash": -0.05},
        "hit": {"head_r": 14, "head_dx": -30, "pliers_r": 25, "scope_r": 20, "eyes": "shut", "mouth": "o", "lean": 10},
        "ko": {"head_r": 10, "eyes": "shut", "mouth": "o", "pliers_r": 40, "scope_r": 30, "lie": True},
        "win": {"pliers_r": -30, "pliers_dy": -60, "eyes": "happy", "mouth": "open", "head_r": -4, "screen": "clock"},
        "special": {"scope_r": -8, "scope_dx": 190, "scope_dy": -30, "head_dx": 20, "wide": 1.3, "screen": "wave", "mouth": "open"},
    }
    out = {}
    for name in POSES:
        pose = poses[name]
        head = P.head.copy()
        H.paint_eyes(head, pose.get("eyes", "open"), pose.get("look", (6, 0)), pose.get("wide", 1.0))
        H.paint_mouth(head, pose.get("mouth", "smile"))
        scope = H.paint_screen(P.scope.copy(), pose.get("screen", "wave"), 0.3)
        lift = 6 * pose.get("breathe", 0.0)
        c = Image.new("RGBA", (W, Hh), (0, 0, 0, 0))
        body = P.body
        if pose.get("breathe"):
            body = body.resize((W, int(Hh * 1.012)), Image.BICUBIC).crop((0, int(Hh * 0.012), W, int(Hh * 1.012)))
        # a wider canvas, so arms swung out stay in the picture
        stage = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
        def put(layer, deg, pivot, dx, dy):
            big = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
            big.alpha_composite(layer, (MX, MT))
            piv = (pivot[0] + MX, pivot[1] + MT)
            stage.alpha_composite(H.rot_layer(big, deg, piv, dx, dy))
        put(body, 0, (0, 0), 0, 0)
        put(head, pose.get("head_r", 0), H.HEAD_PIVOT, pose.get("head_dx", 0), pose.get("head_dy", 0) - lift)
        put(scope, pose.get("scope_r", 0), H.SCOPE_PIVOT, pose.get("scope_dx", 0), pose.get("scope_dy", 0) - lift * 0.6)
        put(P.pliers, pose.get("pliers_r", 0), H.PLIERS_PIVOT, pose.get("pliers_dx", 0), pose.get("pliers_dy", 0) - lift * 0.6)
        out[name] = finish(stage, pose, foot=(MX + W // 2, MT + Hh))
    return out


def finish(stage, pose, foot):
    """squash, hop, lean or lie about the feet; then down to one frame with the feet on FOOT_Y."""
    SW, SH = stage.size
    sq = pose.get("squash", 0.0)
    if sq:
        nw, nh = int(SW * (1 + sq * 0.6)), int(SH * (1 - sq))
        s2 = stage.resize((nw, nh), Image.BICUBIC)
        stage = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
        stage.alpha_composite(s2, ((SW - nw) // 2, SH - nh))
    if pose.get("lean"):
        stage = stage.rotate(pose["lean"], resample=Image.BICUBIC, center=foot)
    hop = pose.get("hop", 0)
    if hop:
        s2 = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
        s2.alpha_composite(stage, (0, -int(hop)))
        stage = s2
    # the frame: SPR_W x SPR_H game px; the figure 72 px tall, feet at FOOT_Y
    k = 72.0 / 956.0
    small = stage.resize((max(1, round(SW * k)), max(1, round(SH * k))), Image.LANCZOS)
    fr = Image.new("RGBA", (SPR_W, SPR_H), (0, 0, 0, 0))
    fx, fy = round(foot[0] * k), round(foot[1] * k)
    fr.alpha_composite(small, (SPR_W // 2 - fx, FOOT_Y - fy))
    return lie_down(fr) if pose.get("lie") else fr


def lie_down(fr):
    """KO: the standing frame turned on its back, centred, lying on the feet's row."""
    r = fr.rotate(90, resample=Image.BICUBIC, expand=True)
    r = r.crop(r.getchannel("A").getbbox())
    out = Image.new("RGBA", (SPR_W, SPR_H), (0, 0, 0, 0))
    out.alpha_composite(r, ((SPR_W - r.width) // 2, FOOT_Y + 2 - r.height))
    return out


# ---------------------------------------------------------------- Kody, drawn in his style
BLUE = (2, 138, 194)
BLUE_D = (1, 104, 150)
INK = (25, 25, 25)
WHITE = (255, 255, 255)


def kody_frames():
    S = 4                                             # supersampling
    out = {}
    poses = {
        "idle0": {}, "idle1": {"breathe": 1},
        "walk0": {"hop": 4, "lean": -3, "arms": (-10, 10)}, "walk1": {"lean": 3, "arms": (10, -10), "squash": 0.03},
        "punch": {"arms": (-10, 0), "reach": 30, "lean": -6, "brow": "mad"},
        "kick": {"foot": 22, "lean": 10, "arms": (-40, 30), "brow": "mad"},
        "block": {"arms": (60, 60), "guard": True, "eyes": "shut", "squash": 0.04},
        "crouch": {"squash": 0.25, "arms": (20, 20)},
        "jump": {"arms": (-70, -70), "eyes": "happy", "squash": -0.06},
        "hit": {"lean": 14, "eyes": "x", "mouth": "o", "arms": (40, -40)},
        "ko": {"eyes": "x", "mouth": "o", "lie": True},
        "win": {"arms": (-140, -140), "eyes": "happy", "mouth": "open"},
        "special": {"arms": (60, 0), "reach": 22, "eyes": "wide", "mouth": "open", "brow": "mad"},
    }
    for name in POSES:
        p = poses[name]
        Wd, Hd = 160 * S, 150 * S
        im = Image.new("RGBA", (Wd, Hd), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        cx, foot = 80 * S, 140 * S
        br = 2 * S if p.get("breathe") else 0
        bw, bh = 56 * S, 54 * S
        top = foot - 2 * bh + br
        w8 = 3 * S                                    # the outline
        # feet
        for side in (-1, 1):
            fx = cx + side * 22 * S
            ext = p.get("foot", 0) * S if side == 1 else 0
            d.ellipse([fx - 12 * S + ext, foot - 9 * S, fx + 12 * S + ext, foot + 5 * S], fill=BLUE_D, outline=INK, width=w8)
        # arms behind the body when down
        def arm(side, ang, front):
            sx = cx + side * (bw - 8 * S)
            sy = top + int(bh * 1.2)
            reach = p.get("reach", 0) * S if (side == 1 and front) else 0
            a = math.radians(ang)
            L = 16 * S + reach
            ex = sx + side * math.cos(a) * L
            ey = sy + math.sin(a) * L
            d.line([sx, sy, ex, ey], fill=INK, width=16 * S)
            d.line([sx, sy, ex, ey], fill=BLUE, width=10 * S)
            r = (10 if reach else 8) * S
            d.ellipse([ex - r, ey - r, ex + r, ey + r], fill=BLUE, outline=INK, width=w8)
            if reach:
                for j in (-1, 0, 1):
                    d.arc([ex - 2 * S, ey + j * 5 * S - 3 * S, ex + 8 * S, ey + j * 5 * S + 3 * S], 270, 90, fill=INK, width=2 * S)
        # body: a round blob, wider at the bottom
        d.ellipse([cx - bw, top, cx + bw, foot + 2 * S], fill=BLUE, outline=INK, width=w8)
        d.ellipse([cx - bw + 10 * S, top + bh, cx + bw - 18 * S, foot - 4 * S], fill=BLUE)
        # ears
        for side in (-1, 1):
            ex = cx + side * 26 * S
            d.ellipse([ex - 7 * S, top - 2 * S, ex + 7 * S, top + 11 * S], fill=BLUE, outline=INK, width=w8)
        d.ellipse([cx - bw + 2 * S, top + 6 * S, cx + bw - 2 * S, foot], fill=BLUE)
        # face, a little to the right
        fx, fy = cx + 8 * S, top + 30 * S
        eyes = p.get("eyes", "open")
        for side in (-1, 1):
            ex = fx + side * 10 * S
            if eyes in ("open", "wide"):
                r = (8 if eyes == "wide" else 7) * S
                d.ellipse([ex - r, fy - r, ex + r, fy + r], fill=WHITE, outline=INK, width=2 * S)
                d.ellipse([ex - 2 * S, fy - 4 * S, ex + 5 * S, fy + 4 * S], fill=INK)
            elif eyes == "happy":
                d.arc([ex - 7 * S, fy - 3 * S, ex + 7 * S, fy + 9 * S], 200, 340, fill=INK, width=3 * S)
            elif eyes == "shut":
                d.line([ex - 7 * S, fy, ex + 7 * S, fy], fill=INK, width=3 * S)
            elif eyes == "x":
                d.line([ex - 5 * S, fy - 5 * S, ex + 5 * S, fy + 5 * S], fill=INK, width=3 * S)
                d.line([ex + 5 * S, fy - 5 * S, ex - 5 * S, fy + 5 * S], fill=INK, width=3 * S)
        brow = p.get("brow")
        if brow == "mad":
            d.line([fx - 18 * S, fy - 13 * S, fx - 4 * S, fy - 8 * S], fill=INK, width=3 * S)
            d.line([fx + 18 * S, fy - 13 * S, fx + 4 * S, fy - 8 * S], fill=INK, width=3 * S)
        else:
            d.line([fx - 16 * S, fy - 12 * S, fx + 16 * S, fy - 12 * S], fill=INK, width=2 * S)
        # nose and teeth
        ty = fy + 11 * S
        d.ellipse([fx - 3 * S, ty - 3 * S, fx + 3 * S, ty + 1 * S], fill=INK)
        mouth = p.get("mouth")
        if mouth in ("o", "open"):
            d.ellipse([fx - 6 * S, ty + 2 * S, fx + 6 * S, ty + (16 if mouth == "open" else 12) * S], fill=(120, 30, 40), outline=INK, width=2 * S)
        d.rectangle([fx - 5 * S, ty + 1 * S, fx + 5 * S, ty + 9 * S], fill=WHITE, outline=INK, width=2 * S)
        d.line([fx, ty + 1 * S, fx, ty + 9 * S], fill=INK, width=2 * S)
        a0, a1 = p.get("arms", (20, 20))
        arm(-1, 90 + a0, False)
        arm(1, 90 - a1 if not p.get("reach") else -10, True)
        if p.get("guard"):
            d.ellipse([fx - 20 * S, fy + 4 * S, fx + 20 * S, fy + 26 * S], fill=BLUE, outline=INK, width=w8)
        out[name] = finish_kody(im, p, (cx, foot), S)
    return out


def finish_kody(im, p, foot, S):
    W, H = im.size
    sq = p.get("squash", 0.0)
    if sq:
        nw, nh = int(W * (1 + sq * 0.6)), int(H * (1 - sq))
        s2 = im.resize((nw, nh), Image.BICUBIC)
        im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        im.alpha_composite(s2, ((W - nw) // 2, H - nh))
    if p.get("lean"):
        im = im.rotate(-p["lean"], resample=Image.BICUBIC, center=foot)
    if p.get("hop"):
        s2 = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        s2.alpha_composite(im, (0, -p["hop"] * S))
        im = s2
    k = 72.0 / (140 * S - 12 * S)                     # Kody as tall as Hammie
    small = im.resize((round(W * k), round(H * k)), Image.LANCZOS)
    fr = Image.new("RGBA", (SPR_W, SPR_H), (0, 0, 0, 0))
    fr.alpha_composite(small, (SPR_W // 2 - round(foot[0] * k), FOOT_Y - round(foot[1] * k)))
    return lie_down(fr) if p.get("lie") else fr


# ---------------------------------------------------------------- power-ups
def icon_frames():
    S, N = 4, 16
    out = {}
    def new():
        im = Image.new("RGBA", (N * S, N * S), (0, 0, 0, 0))
        return im, ImageDraw.Draw(im)
    im, d = new()   # battery
    d.rounded_rectangle([1 * S, 4 * S, 13 * S, 12 * S], 2 * S, fill=(34, 40, 49), outline=INK, width=S)
    d.rectangle([13 * S, 6 * S, 15 * S, 10 * S], fill=INK)
    d.rectangle([3 * S, 6 * S, 11 * S, 10 * S], fill=(94, 224, 138))
    out["battery"] = im
    im, d = new()   # coffee
    d.rounded_rectangle([2 * S, 5 * S, 11 * S, 15 * S], 2 * S, fill=WHITE, outline=INK, width=S)
    d.rectangle([3 * S, 7 * S, 10 * S, 9 * S], fill=(120, 70, 40))
    d.arc([9 * S, 7 * S, 15 * S, 13 * S], 270, 90, fill=INK, width=S)
    for x in (4, 8):
        d.line([x * S, 1 * S, (x + 1) * S, 4 * S], fill=(200, 200, 200), width=S)
    out["coffee"] = im
    im, d = new()   # soldering iron
    d.line([2 * S, 14 * S, 9 * S, 7 * S], fill=INK, width=4 * S)
    d.line([2 * S, 14 * S, 9 * S, 7 * S], fill=(255, 194, 26), width=2 * S)
    d.line([9 * S, 7 * S, 14 * S, 2 * S], fill=(170, 170, 170), width=2 * S)
    d.ellipse([12 * S, 0, 16 * S, 4 * S], fill=(255, 90, 40))
    out["iron"] = im
    im, d = new()   # heart
    d.polygon([(8 * S, 15 * S), (1 * S, 7 * S), (2 * S, 2 * S), (6 * S, 1 * S), (8 * S, 4 * S), (10 * S, 1 * S),
               (14 * S, 2 * S), (15 * S, 7 * S)], fill=(230, 50, 60), outline=INK)
    out["heart"] = im
    return {k: v.resize((N, N), Image.LANCZOS) for k, v in out.items()}


# ---------------------------------------------------------------- C
def c_array(name, im):
    px = im.load()
    vals = []
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            vals.append(KEY if a < 110 else (rgb565(r, g, b) if rgb565(r, g, b) != KEY else KEY ^ 0x20))
    lines = []
    for i in range(0, len(vals), 16):
        lines.append("    " + ",".join("0x%04X" % v for v in vals[i:i + 16]) + ",")
    return "static const uint16_t %s[%d] = {\n%s\n};\n" % (name, len(vals), "\n".join(lines))


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    if "--ref" in sys.argv:
        os.environ["HAMMIE_REF"] = sys.argv[sys.argv.index("--ref") + 1]
    chars = {"kody": kody_frames(), "hammie": hammie_frames()}
    icons = icon_frames()
    h = ["/* SPDX-License-Identifier: GPL-3.0-or-later */",
         "/**", " * @file sprites_gen.h", " * @brief The fighters' poses and the power-ups, RGB565, SPR_KEY = transparent.",
         " *", " * Generated by tools/gen_sprites.py - do not edit.", " */",
         "#ifndef SPRITES_GEN_H_", "#define SPRITES_GEN_H_", "", "#include <stdint.h>", "",
         "#define SPR_W %d" % SPR_W, "#define SPR_H %d" % SPR_H, "#define SPR_FOOT_Y %d" % FOOT_Y,
         "#define SPR_KEY 0x%04X" % KEY, "#define ICON_N 16", "",
         "enum { " + ", ".join("POSE_" + p.upper() for p in POSES) + ", POSE_COUNT };",
         "enum { ICON_BATTERY, ICON_COFFEE, ICON_IRON, ICON_HEART, ICON_COUNT };", ""]
    for cn, frames in chars.items():
        for p in POSES:
            h.append(c_array("SPR_%s_%s" % (cn.upper(), p.upper()), frames[p]))
        h.append("static const uint16_t *const SPR_%s[POSE_COUNT] = { %s };\n" %
                 (cn.upper(), ", ".join("SPR_%s_%s" % (cn.upper(), p.upper()) for p in POSES)))
    for k in ("battery", "coffee", "iron", "heart"):
        h.append(c_array("ICON_%s_PX" % k.upper(), icons[k]))
    h.append("static const uint16_t *const ICONS[ICON_COUNT] = { ICON_BATTERY_PX, ICON_COFFEE_PX, ICON_IRON_PX, ICON_HEART_PX };\n")
    h.append("#endif /* SPRITES_GEN_H_ */\n")
    open(os.path.join(out, "sprites_gen.h"), "w").write("\n".join(h))
    # a preview sheet, 3x
    S = Image.new("RGB", (SPR_W * len(POSES), SPR_H * 2 + 20), (40, 44, 60))
    for r, (cn, frames) in enumerate(chars.items()):
        for i, p in enumerate(POSES):
            S.paste(frames[p], (i * SPR_W, r * SPR_H), frames[p])
    for i, k in enumerate(("battery", "coffee", "iron", "heart")):
        S.paste(icons[k], (4 + i * 20, SPR_H * 2 + 2), icons[k])
    S = S.resize((S.width * 2, S.height * 2), Image.NEAREST)
    S.save(os.path.join(out, "sprites_preview.png"))
    print("ok", os.path.getsize(os.path.join(out, "sprites_gen.h")) // 1024, "KB header")


if __name__ == "__main__":
    main()
