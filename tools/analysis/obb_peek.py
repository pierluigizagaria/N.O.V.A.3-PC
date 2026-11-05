import sys
import zipfile
from collections import Counter


def main(apk, inner):
    with zipfile.ZipFile(apk) as outer:
        with outer.open(inner) as f:
            head = f.read(16)
            print("magic:", head[:4], head.hex())
            f.seek(0)
            try:
                z = zipfile.ZipFile(f)
            except zipfile.BadZipFile as e:
                print("not a zip:", e)
                return
            infos = z.infolist()
            print("entries:", len(infos))
            exts = Counter()
            dirs = Counter()
            comp = Counter()
            for i in infos:
                n = i.filename
                exts[n.rsplit(".", 1)[-1].lower() if "." in n.split("/")[-1] else "(none)"] += 1
                dirs["/".join(n.split("/")[:2])] += 1
                comp[i.compress_type] += 1
            print("compression:", dict(comp))
            print("extensions:", ", ".join("%s(%d)" % kv for kv in exts.most_common(30)))
            print("dirs:", ", ".join("%s(%d)" % kv for kv in dirs.most_common(30)))
            hits = [i.filename for i in infos if any(t in i.filename.lower() for t in ("dxt", "pvr", "atc", "etc", "tegra", "s3tc"))]
            print("format-named files (first 20):", hits[:20], len(hits))


if __name__ == "__main__":
    main(*sys.argv[1:])
