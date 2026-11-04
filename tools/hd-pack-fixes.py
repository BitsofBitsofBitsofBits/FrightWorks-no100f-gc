#!/usr/bin/env python3
"""Fixes known problems in dogerki's HD texture pack, in the game folder's copy of it.

    python tools/hd-pack-fixes.py <pack folder> <disc DATA folder> [--required]

Run by tools/stage-assets right after it mirrors texture_pack\\ScoobyHD into the game folder (Play\\ after every build,
the packaged game's data\\), so it works the same on a fresh clone, an existing checkout and a worktree, and never
touches the shared texture_pack\\. The robocopy mirror puts dogerki's original back each time and this edits it again,
so a second run over an already fixed file changes nothing. Python standard library only; no network, no telemetry.

Each fix names one pack file by its SHA-256. The file is edited only when it is exactly dogerki's original: a file that
is already fixed is left alone quietly, anything else (another pack, a hand-edited file) is left alone with a log line.
The output is deterministic (fixed filters and compression level), so repeated runs on one machine give the same bytes
(zlib's compressed bytes can differ between Python builds; the pixels never do). A fix that is skipped
is logged and the exit code stays 0, unless --required is given (packaging): then a skip exits 1.

Fix 1, Smuggler's Cove sign (issue #107): the pack's file for sign_smugglers is an RGB PNG without alpha, but the
original texture cuts out the two small ship's sails on the sign's roof with alpha. Without it they draw as two opaque
planks. The alpha is read from the disc's own texture at build time (nothing from the game is stored in git), scaled
6x bilinear to the HD size, its edge sharpened a little, and written as the PNG's alpha channel; the colours are not touched.
"""
import hashlib
import os
import struct
import sys
import zlib

# ---- the fix ---------------------------------------------------------------------------------------------------
SIGN_FILE = "tex1_128x128_665c7b880e4081e9_5.png"
SIGN_ORIGINAL_SHA256 = "825098ff89241e5d6fa56150bc2dd1937b682441eb39e96d330a203cfac9c954"
SIGN_HIP = "h001.hip"            # the only scene that has this texture
SIGN_TEXTURE = "sign_smugglers"  # the RWTX asset sign_smugglers.RW3
SIGN_KEY_HASH = 0x665C7B880E4081E9  # xxh64 of the original's base level: the pack file name's hash


def log(text):
    print("HD pack fixes: " + text, flush=True)


# ---- XXH64 (seed 0), to confirm the disc texture is the one the pack file is named after -------------------------
_P1, _P2, _P3, _P4, _P5 = (11400714785074694791, 14029467366897019727, 1609587929392839161,
                           9650029242287828579, 2870177450012600261)
_M = (1 << 64) - 1


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _M


def _round(acc, lane):
    acc = (acc + lane * _P2) & _M
    return (_rotl(acc, 31) * _P1) & _M


def _merge(acc, v):
    acc ^= _round(0, v)
    return (acc * _P1 + _P4) & _M


def xxh64(b):
    n = len(b)
    p = 0
    if n >= 32:
        v1, v2, v3, v4 = (_P1 + _P2) & _M, _P2, 0, (-_P1) & _M
        while p + 32 <= n:
            a, c, d, e = struct.unpack_from("<4Q", b, p)
            v1, v2, v3, v4 = _round(v1, a), _round(v2, c), _round(v3, d), _round(v4, e)
            p += 32
        h = (_rotl(v1, 1) + _rotl(v2, 7) + _rotl(v3, 12) + _rotl(v4, 18)) & _M
        for v in (v1, v2, v3, v4):
            h = _merge(h, v)
    else:
        h = _P5
    h = (h + n) & _M
    while p + 8 <= n:
        (k,) = struct.unpack_from("<Q", b, p)
        h ^= _round(0, k)
        h = (_rotl(h, 27) * _P1 + _P4) & _M
        p += 8
    if p + 4 <= n:
        (k,) = struct.unpack_from("<I", b, p)
        h ^= (k * _P1) & _M
        h = (_rotl(h, 23) * _P2 + _P3) & _M
        p += 4
    while p < n:
        h ^= (b[p] * _P5) & _M
        h = (_rotl(h, 11) * _P1) & _M
        p += 1
    h ^= h >> 33
    h = (h * _P2) & _M
    h ^= h >> 29
    h = (h * _P3) & _M
    h ^= h >> 32
    return h


# ---- the disc's original texture (HIP archive -> RWTX -> GX RGB5A3 alpha) ----------------------------------------
def _hip_assets(data):
    """(type, offset, size, name) of every asset in a HIP file."""
    out = []

    def walk(off, end):
        while off + 8 <= end:
            tag = data[off:off + 4]
            size = struct.unpack(">I", data[off + 4:off + 8])[0]
            body = off + 8
            if tag == b"HIPA":
                off = body
                continue
            if tag in (b"DICT", b"ATOC"):
                walk(body, min(end, body + size))
            elif tag == b"AHDR":
                _aid, typ, aoff, asz = struct.unpack(">I4sII", data[body:body + 16])
                name = ""
                if data[body + 24:body + 28] == b"ADBG":
                    name = data[body + 36:body + 36 + 64].split(b"\0")[0].decode("latin1").strip()
                out.append((typ.decode("latin1"), aoff, asz, name))
            off = body + size

    walk(0, len(data))
    return out


def _rw_chunks(b, off, end):
    while off + 12 <= end:
        t, s, _v = struct.unpack_from("<III", b, off)
        yield t, off + 12, s
        off += 12 + s


def _rwtx_textures(b):
    """(name, width, height, format, base level bytes) of the textures in one RWTX asset (non-palette formats)."""
    for t, o, s in _rw_chunks(b, 0, len(b)):
        if t != 0x16:
            continue
        for t2, o2, s2 in _rw_chunks(b, o, o + s):
            if t2 != 0x15:
                continue
            for t3, o3, s3 in _rw_chunks(b, o2, o2 + s2):
                if t3 != 1:
                    continue
                h = b[o3:o3 + s3]
                name = h[0x18:0x38].split(b"\0")[0].decode("latin1")
                w, hh, _depth, _lv, fmt, _tl = struct.unpack_from(">HHBBBB", h, 0x5C)
                if fmt in (4, 5):  # RGB565 / RGB5A3: 4x4 blocks of 16 bits
                    yield name, w, hh, fmt, bytes(h[0x6C:0x6C + w * hh * 2])
                break


def _find_hip(disc):
    for dirpath, _dirs, files in os.walk(os.path.join(disc, "files")):
        for f in files:
            if f.lower() == SIGN_HIP:
                return os.path.join(dirpath, f)
    return None


def original_alpha(disc):
    """The disc's sign texture as (width, height, rows of 0-255 alpha), or None with a log line when it is missing."""
    hip = _find_hip(disc)
    if hip is None:
        log("no %s under %s: %s left as it is" % (SIGN_HIP, disc, SIGN_FILE))
        return None
    with open(hip, "rb") as f:
        data = f.read()
    for typ, aoff, asz, asset in _hip_assets(data):
        if typ != "RWTX" or asset.lower().rsplit(".", 1)[0] != SIGN_TEXTURE:
            continue
        for _name, w, h, fmt, base in _rwtx_textures(data[aoff:aoff + asz]):
            if fmt != 5:
                continue
            if xxh64(base) != SIGN_KEY_HASH:
                log("the disc's %s is not the texture the pack file is named after: %s left as it is" %
                    (SIGN_TEXTURE, SIGN_FILE))
                return None
            return w, h, _rgb5a3_alpha(base, w, h)
    log("%s not found in %s: %s left as it is" % (SIGN_TEXTURE, hip, SIGN_FILE))
    return None


def _rgb5a3_alpha(base, w, h):
    rows = [[255] * w for _ in range(h)]
    pos = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            for y in range(4):
                for x in range(4):
                    (v,) = struct.unpack_from(">H", base, pos)
                    pos += 2
                    if not v & 0x8000:  # 3-bit alpha: 0..7 -> 0..255
                        rows[by + y][bx + x] = (((v >> 12) & 7) * 255 * 2 + 7) // 14
    return rows


def upscale_alpha(rows, scale):
    """Bilinear upscale by an integer factor (pixel centres aligned, edges clamped), integer arithmetic only."""
    h, w = len(rows), len(rows[0])
    d = 2 * scale

    def taps(n):  # per output index: (low source index, high source index, weight of the high one out of d)
        out = []
        for i in range(n * scale):
            num = 2 * i + 1 - scale  # source position in units of 1/d
            lo = num // d
            f = num - lo * d
            out.append((min(max(lo, 0), n - 1), min(max(lo + 1, 0), n - 1), f))
        return out

    xt, yt = taps(w), taps(h)
    # horizontal pass keeps d times the precision, vertical pass divides it out once with rounding
    horiz = [[a[x0] * (d - f) + a[x1] * f for x0, x1, f in xt] for a in rows]
    out = []
    for y0, y1, f in yt:
        r0, r1 = horiz[y0], horiz[y1]
        out.append(bytes((r0[i] * (d - f) + r1[i] * f + d * d // 2) // (d * d) for i in range(len(xt))))
    return out


# A bilinear 6x upscale of the original's 3-bit alpha has a ramp one original texel (6 HD pixels) wide, which shows as
# a soft haze of the pack's wood colour round the shapes. A smoothstep from 40% to 60% keeps the shape of the original's
# edge (the 50% contour) but makes the transition about one or two HD pixels wide.
_EDGE_LO, _EDGE_HI = 102, 153


def sharpen_edge(rows):
    out = []
    span = _EDGE_HI - _EDGE_LO
    for r in rows:
        row = bytearray(len(r))
        for i, a in enumerate(r):
            t = min(max(a - _EDGE_LO, 0), span) * 255 // span  # 0..255
            row[i] = t * t * (765 - 2 * t) // 65025
        out.append(bytes(row))
    return out


# ---- PNG: read 8-bit RGB / RGBA, write RGBA (stdlib only) --------------------------------------------------------
_PNG_SIG = b"\x89PNG\r\n\x1a\n"


def read_png(data):
    """(width, height, channels, rows of bytes) of a non-interlaced 8-bit RGB or RGBA PNG; ValueError otherwise."""
    if data[:8] != _PNG_SIG:
        raise ValueError("not a PNG")
    pos, idat, ihdr = 8, [], None
    while pos + 8 <= len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break
        pos += 12 + n
    if ihdr is None:
        raise ValueError("no IHDR")
    w, h, depth, ctype, _comp, _filt, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("expected a non-interlaced 8-bit RGB/RGBA PNG")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    if len(raw) != h * (stride + 1):
        raise ValueError("unexpected PNG data size")
    rows, prev = [], bytearray(stride)
    for y in range(h):
        base = y * (stride + 1)
        ft = raw[base]
        cur = bytearray(raw[base + 1:base + 1 + stride])
        if ft == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pred) & 255
        elif ft != 0:
            raise ValueError("bad PNG filter")
        rows.append(bytes(cur))
        prev = cur
    return w, h, bpp, rows


def _chunk(typ, body):
    return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF)


def write_png_rgba(w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)  # filter 0 on every row: slower to write nothing, same bytes every run
    return (_PNG_SIG + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))


def add_alpha(png_bytes, alpha_rows):
    w, h, bpp, rows = read_png(png_bytes)
    if bpp != 3 or len(alpha_rows) != h or len(alpha_rows[0]) != w:
        raise ValueError("size mismatch: PNG %dx%d, alpha %dx%d" % (w, h, len(alpha_rows[0]), len(alpha_rows)))
    out = []
    for rgb, a in zip(rows, alpha_rows):
        row = bytearray(w * 4)
        row[0::4], row[1::4], row[2::4], row[3::4] = rgb[0::3], rgb[1::3], rgb[2::3], a
        out.append(bytes(row))
    return write_png_rgba(w, h, out)


# ---- driver ----------------------------------------------------------------------------------------------------
FIXED, SKIPPED = "fixed", "skipped"  # a file that is already fixed counts as FIXED


def fix_sign(pack, disc):
    """Returns FIXED when the sign file now has its alpha, SKIPPED (with a log line) when it was left as it is."""
    path = os.path.join(pack, SIGN_FILE)
    if not os.path.isfile(path):
        log("%s is not in %s: left as it is" % (SIGN_FILE, pack))
        return SKIPPED
    with open(path, "rb") as f:
        data = f.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SIGN_ORIGINAL_SHA256:
        try:
            has_alpha = read_png(data)[2] == 4
        except (ValueError, zlib.error, struct.error):
            has_alpha = False
        if has_alpha:  # the output of an earlier run (the mirror step puts the original back, so rare)
            return FIXED
        log("%s is not dogerki's original (sha256 %s...): left as it is" % (SIGN_FILE, digest[:12]))
        return SKIPPED
    orig = original_alpha(disc)
    if orig is None:
        return SKIPPED
    ow, oh, rows = orig
    new = add_alpha(data, sharpen_edge(upscale_alpha(rows, _scale_for(data, ow, oh))))
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(new)
    os.replace(tmp, path)
    log("%s: alpha from the original added (%d -> %d bytes)" % (SIGN_FILE, len(data), len(new)))
    return FIXED


def _scale_for(png_bytes, ow, oh):
    w, h = struct.unpack(">II", png_bytes[16:24])  # IHDR width and height
    if w % ow or h % oh or w // ow != h // oh:
        raise ValueError("HD size %dx%d is not a whole multiple of %dx%d" % (w, h, ow, oh))
    return w // ow


def main(argv):
    required = "--required" in argv
    args = [a for a in argv[1:] if a != "--required"]
    if len(args) != 2:
        print(__doc__)
        return 2
    pack, disc = args
    try:
        if not os.path.isdir(pack):
            log("pack folder %s not found" % pack)
            result = SKIPPED
        else:
            result = fix_sign(pack, disc)
    except (OSError, ValueError, zlib.error, struct.error) as e:
        log("%s: %s (left as it is)" % (SIGN_FILE, e))
        result = SKIPPED
    if result == SKIPPED and required:
        log("FAILED: the fix was skipped and --required is set (packaging must not ship the unfixed pack)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
