import struct
import sys
from collections import Counter


def uleb(data, off):
    result = shift = 0
    while True:
        b = data[off]
        off += 1
        result |= (b & 0x7F) << shift
        if b < 0x80:
            return result, off
        shift += 7


def main(path, focus=""):
    data = open(path, "rb").read()
    (s_n, s_off, t_n, t_off, p_n, p_off, f_n, f_off, m_n, m_off, c_n, c_off) = struct.unpack_from("<12I", data, 0x38)

    def string(i):
        o = struct.unpack_from("<I", data, s_off + 4 * i)[0]
        _, o = uleb(data, o)
        return data[o:data.index(b"\0", o)].decode("utf-8", "replace")

    def type_name(i):
        return string(struct.unpack_from("<I", data, t_off + 4 * i)[0])

    def proto(i):
        _, ret, params = struct.unpack_from("<III", data, p_off + 12 * i)
        args = []
        if params:
            n = struct.unpack_from("<I", data, params)[0]
            args = [type_name(struct.unpack_from("<H", data, params + 4 + 2 * k)[0]) for k in range(n)]
        return "(%s)%s" % ("".join(args), type_name(ret))

    def method(i):
        cls, pr, name = struct.unpack_from("<HHI", data, m_off + 8 * i)
        return type_name(cls), string(name), proto(pr)

    pkgs = Counter()
    natives = []
    focus_methods = {}
    for c in range(c_n):
        cls_idx, flags, sup, _, _, _, cdata, _ = struct.unpack_from("<8I", data, c_off + 32 * c)
        cname = type_name(cls_idx)
        pkgs["/".join(cname.split("/")[:4])] += 1
        if not cdata:
            continue
        o = cdata
        sf, o = uleb(data, o)
        inf, o = uleb(data, o)
        dm, o = uleb(data, o)
        vm, o = uleb(data, o)
        for _ in range(2 * (sf + inf)):
            _, o = uleb(data, o)
        for count in (dm, vm):
            idx = 0
            for _ in range(count):
                diff, o = uleb(data, o)
                acc, o = uleb(data, o)
                _, o = uleb(data, o)
                idx += diff
                _, mname, sig = method(idx)
                if acc & 0x100:
                    natives.append("%s.%s%s" % (cname, mname, sig))
                if focus and any(f in cname for f in focus.split(",")):
                    focus_methods.setdefault(cname, []).append(("static " if acc & 0x8 else "") + mname + sig)
    print("classes:", c_n)
    print("packages:", ", ".join("%s(%d)" % kv for kv in pkgs.most_common(25)))
    print("native methods (%d):" % len(natives))
    for n in natives:
        print("  " + n)
    for cname, ms in focus_methods.items():
        print("== %s (%d methods)" % (cname, len(ms)))
        print("  " + " | ".join(ms))


if __name__ == "__main__":
    main(*sys.argv[1:])
