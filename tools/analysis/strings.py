import re
import sys


def main(path, out, minlen=5):
    with open(path, "rb") as f:
        data = f.read()
    pat = re.compile(rb"[\x20-\x7e]{%d,}" % minlen)
    with open(out, "w", encoding="utf-8") as f:
        for m in pat.finditer(data):
            f.write("%#x\t%s\n" % (m.start(), m.group().decode("ascii")))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
