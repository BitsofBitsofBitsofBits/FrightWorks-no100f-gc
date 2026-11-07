"""Print a RenderWare stream as a section tree (little-endian sections, as on this game's disc). Reads only.

    python tools/debug/game/rwdump.py FILE                   the whole file is a stream (a .dff, a saved asset)
    python tools/debug/game/rwdump.py FILE OFFSET SIZE       a stream inside a file (hex), e.g. a MODL's offset + size
    python tools/debug/game/rwdump.py FILE.HIP NAME          an asset of a HIP by name (MODL, RWTX, BSP)
    --hex N                                                  bytes of each data section to print (default 64)

Exit code 1 if a section runs past the end of the stream. MODL and BSP are RW 3.x with no NativeData section (the game
instances geometry at load); most are version 0x310. RWTX is a texture dictionary (0x0C02FFFF) holding GameCube native
textures, with big-endian contents.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hipedit import Hip

NAMES = {1: "Struct", 2: "String", 3: "Extension", 6: "Texture", 7: "Material", 8: "MaterialList", 0xB: "World",
         0xE: "FrameList", 0xF: "Geometry", 0x10: "Clump", 0x14: "Atomic", 0x15: "TextureNative",
         0x16: "TextureDictionary", 0x1A: "GeometryList", 0x116: "SkinPLG", 0x11E: "HAnimPLG", 0x120: "MatEffects",
         0x50E: "BinMeshPLG", 0x510: "NativeData"}
CONTAINERS = {3, 6, 7, 8, 0xB, 0xE, 0xF, 0x10, 0x14, 0x15, 0x16, 0x1A}


def walk(d, off, end, limit, depth, visit):
    """Calls visit(off, depth, type, size, version, overruns_parent) per section. Returns (next offset, ok).
    Some disc clumps give a size that stops short of their last Atomic (the game goes by the atomic count), so a child
    may run past its parent's end; only a section running past the whole stream is an error."""
    ok = True
    while off + 12 <= end:
        t, s, v = struct.unpack("<III", d[off:off+12])
        if off + 12 + s > limit:
            print("  " * depth + "%06X !! bad section %#x size %#x" % (off, t, s))
            return limit, False
        visit(off, depth, t, s, v, off + 12 + s > end)
        if t in CONTAINERS:
            child_end, child_ok = walk(d, off + 12, off + 12 + s, limit, depth + 1, visit)
            ok = ok and child_ok
            off = max(off + 12 + s, child_end)
        else:
            off += 12 + s
    return off, ok


def dump(d, off, end, depth, maxhex):
    """Prints the tree; returns False if a section runs past the stream (not a stream, or a wrong offset)."""
    def show(o, dep, t, s, v, over):
        data = "" if t in CONTAINERS else " " + d[o+12:o+12+min(s, maxhex)].hex()
        print("  " * dep + "%06X %s size=%#x ver=%#x%s%s" % (o, NAMES.get(t, hex(t)), s, v, data, " (past parent's size)" if over else ""))
    return walk(d, off, end, end, depth, show)[1]


def main():
    args = sys.argv[1:]
    maxhex = 64
    if "--hex" in args:
        i = args.index("--hex")
        maxhex = int(args[i+1])
        del args[i:i+2]
    if len(args) not in (1, 2, 3):
        sys.exit(__doc__)
    d = open(args[0], "rb").read()
    start, size = 0, len(d)
    if len(args) == 3:
        start, size = int(args[1], 16), int(args[2], 16)
    elif len(args) == 2:
        hits = [a for a in Hip(d).assets if a.name.lower() == args[1].lower()]
        if not hits:
            sys.exit("no asset named " + args[1])
        start, size = hits[0].off, len(hits[0].data)
        print("%s %s at %#x, %#x bytes" % (hits[0].typ.decode("latin1"), hits[0].name, start, size))
    sys.exit(0 if dump(d, start, start + size, 0, maxhex) else 1)


if __name__ == "__main__":
    main()
