"""Builds the per-controller button prompt icons (Play/UserData/button_icons).

The game draws its button prompts from one GameCube button picture (an atlas: Y, X, A, B). The game puts the atlas
together at run time (aurora texture_replacement.cpp) from these files, so the prompts can follow the player's button
mapping (hle/input/pad.cpp):
    button_icons/layout.txt                 the atlas's file name and size, the icon size, and where each GameCube
                                            button's icon goes (top-left corner)
    button_icons/<family>/<button>.png      one icon per pad button, named like the [controller] mapping
                                            (south, east, west, north), for xbox, playstation and nintendo; also
                                            back and left_shoulder, which only the in-game hotkey guide shows
                                            (settings_overlay.cpp, not in the atlas)
    button_icons/keyboard/<key>.png         the keys on the hotkey guide (f1, f2, f3, f8, f10, f11, escape)
Icons come from Kenney's Input Prompts (CC0, tools/button-art: only the ones used; the whole pack is not in the repo).
Every icon is drawn at the size of the biggest GameCube button (A), centred where that button is and kept inside its
quarter of the atlas; the GameCube B button is smaller. The atlas is the HD pack's size.

    python tools/make-button-icons.py [--out Play/UserData/button_icons]
Needs ImageMagick (magick) to render the SVGs, and the HD pack in texture_pack/ScoobyHD for the layout.
"""
import argparse
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "tools", "button-art")
ATLAS = "tex1_64x64_f05a599cb8feb028_14.png"
# Pad buttons by position (SDL / controller_button_names.h names) -> art.
FAMILIES = {
    "xbox": {"south": "xbox_button_color_a", "east": "xbox_button_color_b", "west": "xbox_button_color_x",
             "north": "xbox_button_color_y"},
    "playstation": {"south": "playstation_button_color_cross", "east": "playstation_button_color_circle",
                    "west": "playstation_button_color_square", "north": "playstation_button_color_triangle"},
    "nintendo": {"south": "switch_button_b", "east": "switch_button_a", "west": "switch_button_y", "north": "switch_button_x"},
}
# Buttons the hotkey guide shows (white art, drawn with a dark symbol like Nintendo's buttons).
EXTRAS = {
    "xbox": {"back": "xbox_button_view", "left_shoulder": "xbox_lb"},
    "playstation": {"back": "playstation4_button_share", "left_shoulder": "playstation_trigger_l1"},
    "nintendo": {"back": "switch_button_minus", "left_shoulder": "switch_button_l"},
}
# Keys the hotkey guide shows (white keycaps, dark lettering).
KEYBOARD = ["f1", "f2", "f3", "f8", "f10", "f11", "escape"]


def regions(img):
    """Bounding boxes of the atlas's four buttons, by quadrant: Y top-left, X top-right, A bottom-left, B bottom-right."""
    a = img.getchannel("A").point(lambda v: 255 if v > 128 else 0)
    w, h = img.size
    out = {}
    for name, (x0, y0) in {"Y": (0, 0), "X": (w // 2, 0), "A": (0, h // 2), "B": (w // 2, h // 2)}.items():
        box = a.crop((x0, y0, x0 + w // 2, y0 + h // 2)).getbbox()
        assert box, f"no {name} button in the atlas"
        out[name] = (x0 + box[0], y0 + box[1], x0 + box[2], y0 + box[3])
    return out


def render_svg(svg, size, tmp, dark_symbol):
    png = os.path.join(tmp, os.path.basename(svg) + f".{size}.png")
    subprocess.run(["magick", "-background", "none", "-density", "600", svg, "-resize", f"{size}x{size}", png], check=True)
    icon = Image.open(png).convert("RGBA")
    # The symbol is cut out of the button: fill what the outside can't reach (white, dark on Nintendo's white buttons).
    outside = Image.new("L", (icon.width + 2, icon.height + 2), 0)
    outside.paste(icon.getchannel("A").point(lambda v: 255 if v > 32 else 0), (1, 1))
    ImageDraw.floodfill(outside, (0, 0), 128)
    holes = outside.crop((1, 1, icon.width + 1, icon.height + 1)).point(lambda v: 255 if v == 0 else 0)
    fill = Image.new("RGBA", icon.size, (40, 40, 40, 255) if dark_symbol else (255, 255, 255, 255))
    filled = Image.alpha_composite(Image.composite(fill, Image.new("RGBA", icon.size), holes), icon)
    return filled


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "Play", "UserData", "button_icons"))
    a = ap.parse_args()
    base = Image.open(os.path.join(ROOT, "texture_pack", "ScoobyHD", ATLAS)).convert("RGBA")
    boxes = regions(base)
    size = max(min(x1 - x0, y1 - y0) for x0, y0, x1, y1 in boxes.values())
    os.makedirs(a.out, exist_ok=True)
    lines = [f"atlas {ATLAS} {base.width} {base.height}", f"icon {size}"]
    for name, (x0, y0, x1, y1) in boxes.items():
        # Centred on the GameCube button, but kept inside its quarter of the atlas.
        qx, qy = x0 // (base.width // 2) * (base.width // 2), y0 // (base.height // 2) * (base.height // 2)
        left = min(max((x0 + x1 - size) // 2, qx), qx + base.width // 2 - size)
        top = min(max((y0 + y1 - size) // 2, qy), qy + base.height // 2 - size)
        lines.append(f"{name} {left} {top}")
    with open(os.path.join(a.out, "layout.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", os.path.join(a.out, "layout.txt"))
    with tempfile.TemporaryDirectory() as tmp:
        for family, buttons in FAMILIES.items():
            os.makedirs(os.path.join(a.out, family), exist_ok=True)
            for button, art in buttons.items():
                icon = render_svg(os.path.join(ART, family, art + ".svg"), size, tmp, family == "nintendo")
                assert icon.size == (size, size), (art, icon.size)
                icon.save(os.path.join(a.out, family, button + ".png"))
            for button, art in EXTRAS[family].items():
                icon = render_svg(os.path.join(ART, family, art + ".svg"), size, tmp, True)
                assert icon.size == (size, size), (art, icon.size)
                icon.save(os.path.join(a.out, family, button + ".png"))
            print("wrote", os.path.join(a.out, family), ", ".join([*buttons, *EXTRAS[family]]))
        os.makedirs(os.path.join(a.out, "keyboard"), exist_ok=True)
        for key in KEYBOARD:
            icon = render_svg(os.path.join(ART, "keyboard", f"keyboard_{key}.svg"), size, tmp, True)
            assert icon.size == (size, size), (key, icon.size)
            icon.save(os.path.join(a.out, "keyboard", key + ".png"))
        print("wrote", os.path.join(a.out, "keyboard"), ", ".join(KEYBOARD))


if __name__ == "__main__":
    main()
