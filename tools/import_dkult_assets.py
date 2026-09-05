"""Import the DKUlt pilot's two reloc sources. Never reads or modifies a ROM."""
import argparse
import json
from pathlib import Path
import re

IDS = {"main": 0x7000, "character": 0x7001}


def chain(data, start, internal=False):
    seen = set()
    targets = []
    while start != 0xFFFF:
        offset = start * 4
        if offset + 4 > len(data) or start in seen:
            raise ValueError("invalid/cyclic relocation chain")
        seen.add(start)
        start = int.from_bytes(data[offset:offset + 2], "big")
        target = int.from_bytes(data[offset + 2:offset + 4], "big") * 4
        if internal and target >= len(data):
            raise ValueError("internal target outside asset")
        targets.append(target)
    return targets


def import_assets(source, output):
    config = (source / "config.yaml").read_text(encoding="utf-8")
    files, blobs = [], {}
    for name, file_id in IDS.items():
        # Deliberately scoped to the pinned DKUlt YAML's two offset arrays.
        match = re.search(r'^\s+' + name + r':\s*\["([0-9A-Fa-f]+)",\s*"([0-9A-Fa-f]+)"\]', config, re.M)
        if not match:
            raise ValueError("missing offsets for " + name)
        offsets = [int(value, 16) for value in match.groups()]
        if any(value % 4 or value > 0x3FFFC for value in offsets):
            raise ValueError("invalid byte offset")
        intern, external = [value // 4 for value in offsets]
        data = (source / (name + ".bin")).read_bytes()
        if not data or len(data) % 4:
            raise ValueError("invalid asset size")
        dependencies = []
        for line in (source / (name + "_reqlist.txt")).read_text().splitlines():
            if not line.strip() or line.startswith("END OF"):
                continue
            value = line.split()[0]
            dep = IDS["character"] if value == "${CHARACTER}" else int(value, 16)
            if not 0 <= dep < 0xFFFF:
                raise ValueError("dependency id exceeds u16")
            dependencies.append(dep)
        chain(data, intern, True)
        if len(chain(data, external)) != len(dependencies):
            raise ValueError("dependency count mismatch for " + name)
        blobs[name] = data
        files.append(dict(id=file_id, path=name + ".bin", size=len(data),
                          intern_words=intern, extern_words=external,
                          dependencies=dependencies))
    # Check cross-pack targets too, before writing any output.
    for entry in files:
        for target, dep in zip(chain(blobs[Path(entry["path"]).stem], entry["extern_words"]), entry["dependencies"]):
            if dep == IDS["character"] and target >= len(blobs["character"]):
                raise ValueError("character dependency target outside asset")
    output.mkdir(parents=True, exist_ok=True)
    for name, data in blobs.items():
        (output / (name + ".bin")).write_bytes(data)
    (output / "assets.json").write_text(json.dumps(dict(version=1, files=files), indent=2) + "\n", encoding="utf-8")
    for entry in files:
        print(f'{entry["path"]}: {entry["size"]} bytes, id={entry["id"]:#06x}, {len(entry["dependencies"])} dependencies')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="extra_characters/DKUlt directory")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "workspace/dkult/assets/character")
    args = parser.parse_args()
    import_assets(args.source, args.output)
