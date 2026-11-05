"""Render the README banner: launcher artwork, original logo and a matching "PC".

Inputs are the local launcher assets (never committed by this tool):
  local/launcher/background-a.png   our generated background
  local/launcher/logo-original.png  original N.O.V.A. 3 logo with alpha
  local/launcher/neuropol.ttf       original Neuropol font, for the tagline only
"PC" is the owner-drawn mark in src/launcher/pc_mark.h, with the logo's face gradient and rim, kept flat.
Output: assets/branding/banner.png, the full background (no crop, no added shade)
at 1600 px wide. Committing that image is the owner's decision: it shows the
original logo, a Gameloft trademark.
Requires Pillow.

    python tools/make_banner.py
    python tools/make_banner.py --font other.ttf --output preview.png
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'local/launcher'
BRANDING = ROOT / 'assets/branding'  # Committed copies used by CI and as a fallback.
WIDTH = 1600  # Output width; the height follows the background's own aspect.


def letter_band(logo):
    """Top and bottom rows of the N.O.V.A lettering, measured from the logo's alpha.

    Only the left half is sampled, so the larger "3" and its brackets do not count.
    """
    alpha = logo.getchannel('A').crop((0, 0, logo.width // 2, logo.height))
    opaque = alpha.point(lambda v: 255 if v > 128 else 0)
    # Row coverage via a 1-pixel-wide box resize: mean opacity per row.
    means = opaque.resize((1, alpha.height), Image.BOX).tobytes()
    rows = [v * alpha.width / 255 for v in means]
    # The letters are the longest run of well-covered rows; thin rules and
    # bracket lines above or below form shorter runs.
    best, start = (0, -1), None
    for y, count in enumerate(rows + [0]):
        if count > alpha.width * 0.3:
            start = y if start is None else start
        elif start is not None:
            best = max(best, (y - start, start))
            start = None
    if best[0] == 0:
        return round(logo.height * 0.25), round(logo.height * 0.7)
    return best[1], best[1] + best[0] - 1


# The "PC" mark drawn by the project owner (src/launcher/pc_mark.h). It is
# scaled up, smoothed and finished like the N.O.V.A lettering (flat, no depth).
PC_MARK_SOURCE = ROOT / 'src/launcher/pc_mark.h'


def pc_mark():
    """Rows of the mark, read from the launcher header (single source of truth)."""
    text = PC_MARK_SOURCE.read_text(encoding='ascii')
    return text.split('R"PCMARK(', 1)[1].split(')PCMARK"', 1)[0].strip().split('\n')


def mark_mask(height, scale=4):
    """Supersampled mask of the PC mark whose ink is `height` pixels tall (times scale)."""
    rows = pc_mark()
    cell = Image.new('L', (len(rows[0]), len(rows)))
    cell.putdata([255 if ch == '#' else 0 for row in rows for ch in row])
    factor = height * scale / len(rows)
    size = (round(cell.width * factor), round(cell.height * factor))
    # Nearest-neighbour upscale, then blur + threshold turns 1-cell stair steps
    # into straight 45-degree edges while keeping corners sharp enough.
    big = cell.resize(size, Image.NEAREST).filter(ImageFilter.GaussianBlur(factor * 0.55))
    return big.point(lambda v: 255 if v >= 128 else 0)


# Finish shared with the launcher (skin.cpp): the logo lettering's white-to-pale
# blue face and dark teal rim, without the 3D extrusion.
FACE_TOP, FACE_BOTTOM, RIM = (250, 253, 253), (184, 228, 236), (10, 38, 46)


def logo_letters(cap):
    """Render the mark with the logo's face gradient and rim, kept flat (no depth)."""
    scale = 4
    mark = mark_mask(cap, scale)
    pad = round(0.06 * cap * scale)
    size = (mark.width + 2 * pad, mark.height + 2 * pad)
    face = Image.new('L', size)
    face.paste(mark, (pad, pad))
    grow = 2 * round(0.022 * cap * scale) + 1
    rim = face.filter(ImageFilter.MaxFilter(grow)) if grow > 1 else face

    result = Image.new('RGBA', size, (0, 0, 0, 0))
    result.paste(RIM + (255,), (0, 0), rim)
    gradient = Image.new('RGBA', size)
    draw = ImageDraw.Draw(gradient)
    for y in range(size[1]):
        t = min(1, max(0, (y - pad) / mark.height))
        draw.line([(0, y), (size[0], y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(FACE_TOP, FACE_BOTTOM)) + (255,))
    result.paste(gradient, (0, 0), face)
    return result.resize((size[0] // scale, size[1] // scale), Image.LANCZOS), pad // scale


def find_font(path):
    """Neuropol for the tagline (optional)."""
    for candidate in ([Path(path)] if path else [ART / 'neuropol.ttf', ROOT / 'local/input/fonts/neuropol.ttf']):
        if candidate.is_file():
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser(description='Render assets/branding/banner.png')
    def art(name):
        return ART / name if (ART / name).is_file() else BRANDING / name
    parser.add_argument('--background', default=art('background-a.png'))
    parser.add_argument('--logo', default=art('logo-original.png'))
    parser.add_argument('--font')
    parser.add_argument('--tagline', default='UNOFFICIAL NATIVE WINDOWS PORT')
    parser.add_argument('--output', default=ROOT / 'assets/branding/banner.png')
    args = parser.parse_args()
    font_path = find_font(args.font)

    # The full, uncropped background without any added shading.
    background = Image.open(args.background).convert('RGB')
    height = round(background.height * WIDTH / background.width)
    canvas = background.resize((WIDTH, height), Image.LANCZOS).convert('RGBA')
    logo = Image.open(args.logo).convert('RGBA')
    logo_width = round(WIDTH * 0.40)
    logo = logo.resize((logo_width, round(logo.height * logo_width / logo.width)), Image.LANCZOS)
    x, y = round(WIDTH * 0.055), round(height * 0.5 - logo.height * 0.62)

    # "PC" in the logo's lettering style, at the N.O.V.A cap height and baseline.
    letter_top, letter_bottom = letter_band(logo)
    pc, pad = logo_letters(letter_bottom - letter_top + 1)
    pc_at = (x + logo.width + round(logo.width * 0.03) - pad, y + letter_top - pad)

    # Soft shadow under logo and "PC" lifts them off the artwork.
    shadow = Image.new('L', canvas.size)
    shadow.paste(logo.getchannel('A'), (x, y + 6))
    shadow.paste(pc.getchannel('A'), (pc_at[0], pc_at[1] + 6), pc.getchannel('A'))
    shadow = shadow.filter(ImageFilter.GaussianBlur(10)).point(lambda v: v * 0.7)
    canvas = Image.composite(Image.new('RGBA', canvas.size, (0, 0, 0, 255)), canvas, shadow)
    canvas.alpha_composite(logo, (x, y))
    canvas.alpha_composite(pc, pc_at)

    if args.tagline and font_path:
        small = ImageFont.truetype(str(font_path), round(height * 0.024))
        draw = ImageDraw.Draw(canvas)
        tx, ty = x + round(logo.width * 0.02), round(y + logo.height + height * 0.03)
        for ch in args.tagline:  # Letter-spaced, as on the game's menus.
            draw.text((tx, ty), ch, font=small, fill=(166, 206, 220, 255))
            tx += small.getlength(ch) + small.size * 0.22

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert('RGB').save(output, optimize=True)
    print(f'banner: {output} {WIDTH}x{height}')


if __name__ == '__main__':
    main()
