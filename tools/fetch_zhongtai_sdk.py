"""Download pinned official XTP SDK outside the repository; never handles credentials."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen
from urllib.parse import quote
COMMIT = "78cc395313cfc8835feef89342b611f89731e9ef"
PREFIX = "XTP_API_20250806_2.2.50.8/bin/"
LIBRARY_HASH = "f6d4b688c10306e1699330892c4275c8f8cb36fda541c8ced28fd23713f364ee"
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    destination = args.destination.resolve()
    workspace = Path(__file__).resolve().parents[2]
    if destination.is_relative_to(workspace):
        parser.error("SDK destination must be outside the Trader workspace")
    tree = json.load(urlopen("https://api.github.com/repos/ztsec/xtp_api_python/git/trees/" + COMMIT + "?recursive=1"))
    hashes = {}
    for entry in tree["tree"]:
        name = entry["path"]
        if not name.startswith(PREFIX):
            continue
        relative = name[len(PREFIX):]
        if not (relative.startswith("include/") and relative.endswith(".h") or relative == "linux_glibc_2.17/libxtptraderapi.so"):
            continue
        data = urlopen("https://raw.githubusercontent.com/ztsec/xtp_api_python/" + COMMIT + "/" + quote(name)).read()
        checksum = hashlib.sha256(data).hexdigest()
        if relative.endswith(".so") and checksum != LIBRARY_HASH:
            raise RuntimeError("SDK library checksum mismatch")
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        hashes[relative] = checksum
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "manifest.json").write_text(json.dumps({"version": "2.2.50.8", "commit": COMMIT, "sha256": hashes}, indent=2), encoding="utf-8")
    print(destination)
if __name__ == "__main__":
    main()
