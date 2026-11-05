import hashlib
import sys
import zipfile


def main(archive, *members):
    with zipfile.ZipFile(archive) as z:
        for name in members:
            h = hashlib.sha256()
            with z.open(name) as f:
                while True:
                    chunk = f.read(8 << 20)
                    if not chunk:
                        break
                    h.update(chunk)
            print(h.hexdigest(), name, flush=True)


if __name__ == "__main__":
    main(*sys.argv[1:])
