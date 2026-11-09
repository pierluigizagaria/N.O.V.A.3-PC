"""Render the README banner with the approved combined launcher logo.

Inputs are the local launcher assets (never committed by this tool):
  local/launcher/background-a.png   our generated background
  local/launcher/logo-pc.png        approved N.O.V.A. 3 PC logo with alpha
  local/launcher/neuropol.ttf       original Neuropol font, for the tagline only
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
    parser.add_argument('--logo', default=art('logo-pc.png'))
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
    # Keep the original N.O.V.A. 3 portion at 40% of the banner width while
    # following the approved PC lettering width in the combined artwork.
    logo_width = round(WIDTH * 0.40 * logo.width / 1340)
    logo = logo.resize((logo_width, round(logo.height * logo_width / logo.width)), Image.LANCZOS)
    x, y = round(WIDTH * 0.055), round(height * 0.5 - logo.height * 0.62)

    # One image preserves the approved logo geometry and internal spacing.
    shadow = Image.new('L', canvas.size)
    shadow.paste(logo.getchannel('A'), (x, y + 6))
    shadow = shadow.filter(ImageFilter.GaussianBlur(10)).point(lambda v: v * 0.7)
    canvas = Image.composite(Image.new('RGBA', canvas.size, (0, 0, 0, 255)), canvas, shadow)
    canvas.alpha_composite(logo, (x, y))

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
