#!/usr/bin/env python3
"""ffxiii2-nx (GitHub package): any picture -> the NRO icon, a 256x256 baseline JPEG (what elf2nro / the Homebrew Menu
expect), for tools/gh/build_switch.ps1 -Icon and the builder window.

  python tools/gh/make_icon.py <picture> <out.jpg> [--mode fit|fill|stretch] [--background black|white|#rrggbb]

Reads every format Pillow reads: PNG, JPEG, BMP, GIF (first frame), TIFF, WebP, ICO / CUR (the largest size), TGA, DDS,
PSD (merged image), PCX, PPM, QOI, AVIF (Pillow 11.2+), and HEIC/HEIF with the optional pillow-heif. SVG pictures are
rasterised with the optional cairosvg. The phone's rotation (EXIF orientation) is applied, transparency goes on the
background colour, CMYK, 16-bit and palette pictures become 8-bit RGB.

  fit      the whole picture, scaled to fit in 256x256, centred on the background colour (default)
  fill     scaled to cover 256x256, the overflow cropped evenly (no bars)
  stretch  scaled to exactly 256x256 (the aspect ratio is not kept)

Prints the input's format and size and the output's bytes. Exit code 0 = written, 1 = could not read the picture.
"""
import argparse
import io
import os
import sys

SIZE = 256
MAX_BYTES = 120 * 1024   # an NRO icon stays small; quality is lowered until it fits


def load(path):
    from PIL import Image, ImageOps, ImageSequence
    try:
        import pillow_heif  # optional: HEIC / HEIF / AVIF on older Pillow
        pillow_heif.register_heif_opener()
    except Exception:
        pass
    if path.lower().endswith(('.svg', '.svgz')):
        try:
            import cairosvg
        except Exception:
            sys.exit('make_icon: SVG pictures need the Python module cairosvg; save the picture as PNG instead')
        data = cairosvg.svg2png(url=path, output_width=1024, output_height=1024)
        img = Image.open(io.BytesIO(data))
        img.load()
        return img, 'SVG'
    img = Image.open(path)
    fmt = img.format or '?'
    if getattr(img, 'is_animated', False):
        img = next(iter(ImageSequence.Iterator(img))).copy()
    img.load()
    try:
        img = ImageOps.exif_transpose(img)
    except Exception:
        pass
    return img, fmt


def to_rgba(img):
    from PIL import Image
    if img.mode in ('I;16', 'I;16B', 'I;16L', 'I'):
        img = img.point(lambda v: v / 256).convert('L')
    elif img.mode == 'F':
        img = img.convert('L')
    if img.mode == 'CMYK':
        img = img.convert('RGB')
    if img.mode == 'P':
        img = img.convert('RGBA' if 'transparency' in img.info else 'RGB')
    if img.mode in ('LA', 'L', '1', 'RGB', 'YCbCr', 'LAB', 'HSV'):
        img = img.convert('RGBA')
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    return img


def parse_colour(text):
    from PIL import ImageColor
    return ImageColor.getrgb(text)[:3]


def make(src, dst, mode='fit', background='black'):
    from PIL import Image
    img, fmt = load(src)
    w0, h0 = img.size
    img = to_rgba(img)
    bg = parse_colour(background)
    resample = Image.Resampling.LANCZOS
    w, h = img.size
    if mode == 'stretch':
        img = img.resize((SIZE, SIZE), resample)
        x = y = 0
    elif mode == 'fill':
        s = max(SIZE / w, SIZE / h)
        nw, nh = max(SIZE, round(w * s)), max(SIZE, round(h * s))
        img = img.resize((nw, nh), resample)
        left, top = (nw - SIZE) // 2, (nh - SIZE) // 2
        img = img.crop((left, top, left + SIZE, top + SIZE))
        x = y = 0
    else:
        s = min(SIZE / w, SIZE / h)
        nw, nh = max(1, round(w * s)), max(1, round(h * s))
        img = img.resize((nw, nh), resample)
        x, y = (SIZE - nw) // 2, (SIZE - nh) // 2
    canvas = Image.new('RGBA', (SIZE, SIZE), bg + (255,))
    canvas.alpha_composite(img, (x, y))
    out = canvas.convert('RGB')
    for quality in (95, 90, 85, 80, 70, 60):
        buf = io.BytesIO()
        out.save(buf, 'JPEG', quality=quality, progressive=False, optimize=True, subsampling='4:2:0')
        if buf.tell() <= MAX_BYTES:
            break
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(buf.getvalue())
    print(f'icon: {os.path.basename(src)} ({fmt}, {w0}x{h0}) -> {dst} (256x256 JPEG, {mode}, {buf.tell()} bytes)')


def main():
    ap = argparse.ArgumentParser(description='Any picture -> the 256x256 JPEG icon of an NRO.')
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--mode', choices=('fit', 'fill', 'stretch'), default='fit')
    ap.add_argument('--background', default='black')
    a = ap.parse_args()
    try:
        make(a.src, a.dst, a.mode, a.background)
    except SystemExit:
        raise
    except Exception as e:  # unreadable, truncated or unknown format
        print(f'make_icon: cannot read {a.src} as a picture: {e}', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
