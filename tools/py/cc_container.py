"""Chrono Cross container parsers: cpt (room .bin / .ctd) and drp (room .prd).

"""
import struct

DRP_MAGIC = b"drp\x00"
CPT_PLACEHOLDER = 0x73277449      # "It's" - CDMAKE dummy file


class FormatError(Exception):
    pass


def parse_cpt(data):
    """cpt = u32 count, u32 off[count], u32 eof.  Returns list of bytes
    (sub-files).  Raises FormatError on a malformed table."""
    if len(data) < 8:
        raise FormatError("too short for cpt")
    count = struct.unpack_from("<I", data, 0)[0]
    if count == CPT_PLACEHOLDER:
        raise FormatError("placeholder file")
    if count == 0 or count > 4096 or 4 + 4 * (count + 1) > len(data):
        raise FormatError("bad cpt count %d" % count)
    offs = struct.unpack_from("<%dI" % (count + 1), data, 4)
    for i in range(count):
        if offs[i] > offs[i + 1] or offs[i + 1] > len(data):
            raise FormatError("cpt offsets not monotonic / out of range at %d" % i)
    return [data[offs[i]:offs[i + 1]] for i in range(count)]


# drp subfile type names
DRP_TYPES = {
    0x01: "drp", 0x02: "mesh", 0x03: "timinfo", 0x04: "tim", 0x05: "minst",
    0x07: "unk07", 0x0A: "unk0A", 0x0B: "mdl", 0x0C: "unk0C", 0x10: "unk10",
    0x12: "mesh_bf", 0x15: "timinfo2", 0x16: "mseq", 0x19: "anim",
    0x1A: "unk1A", 0x25: "lzss",
}


class DrpEntry:
    def __init__(self, index, offset, name, type_, length, data):
        self.index, self.offset, self.name = index, offset, name
        self.type, self.length, self.data = type_, length, data

    def __repr__(self):
        return "<DrpEntry %d %r type=%02X len=%d>" % (self.index, self.name, self.type, self.length)


def parse_drp(data):
    """drp: 'drp\0' + 4 zero bytes + u16 (num*64) + 2 zero; then num u32 abs
    offsets.  Each entry: 12-byte header (00 00 00 00, 4 name chars, type byte,
    24-bit LE value = 16 * payload length) followed by data up to the next offset / EOF.
    Returns a list of DrpEntry.  `length` is value/16; `data` is the span to the next entry."""
    if data[:4] != DRP_MAGIC:
        raise FormatError("not a drp")
    num = struct.unpack_from("<H", data, 8)[0] // 64
    if num > 4096 or 12 + 4 * num > len(data):
        raise FormatError("bad drp count %d" % num)
    offs = list(struct.unpack_from("<%dI" % num, data, 12))
    ends = offs[1:] + [len(data)]
    out = []
    for i, (o, e) in enumerate(zip(offs, ends)):
        if o + 12 > len(data) or e < o + 12 or e > len(data):
            raise FormatError("drp entry %d out of range (%d..%d)" % (i, o, e))
        if data[o:o + 4] != b"\0\0\0\0":
            raise FormatError("drp entry %d header not zero-prefixed" % i)
        name = data[o + 4:o + 8].split(b"\0")[0].decode("latin1")
        type_ = data[o + 8]
        ln = int.from_bytes(data[o + 9:o + 12], "little") // 16   # raw value = 16 * length [C]
        out.append(DrpEntry(i, o, name, type_, ln, data[o + 12:e]))
    return out
