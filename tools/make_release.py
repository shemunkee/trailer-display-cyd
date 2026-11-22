"""Build and sign an OTA release (SPEC.md 5.7). Run from the project folder:
    python tools/make_release.py

1. Bump FW_VERSION in include/version.h and commit first.
2. This builds both board types, then puts in dist/v<version>/:
     firmware-cyd_ili9341.bin, firmware-cyd_st7789.bin
     manifest.json  (version, and size + SHA-256 of each file)
     manifest.sig   (manifest signed with your private key)
3. Make a GitHub release with tag v<version> and attach those four files
   (the script prints how). Displays find it within a day, or straight away
   with Settings > Data source > Check for updates.

The private key (from tools/ota_keygen.py) is read from
%USERPROFILE%\\.victron_cyd\\ota_signing_key.pem and never copied anywhere.
"""

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEY = Path.home() / ".victron_cyd" / "ota_signing_key.pem"
ENVS = ["cyd_ili9341", "cyd_st7789"]


def openssl():
    found = shutil.which("openssl")
    if found:
        return found
    for p in (r"C:\Program Files\Git\usr\bin\openssl.exe", r"C:\Program Files\Git\mingw64\bin\openssl.exe"):
        if Path(p).exists():
            return p
    sys.exit("openssl not found. Install Git for Windows, or put openssl on the PATH.")


def define(text, name):
    m = re.search(rf'#define {name} "([^"]*)"', text)
    if not m:
        sys.exit(f"{name} not found in include/version.h")
    return m.group(1)


def main():
    allow_dirty = "--allow-dirty" in sys.argv
    version_h = (ROOT / "include" / "version.h").read_text()
    version = define(version_h, "FW_VERSION")
    repo = define(version_h, "OTA_REPO")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"FW_VERSION '{version}' must look like 1.2.3")
    if not repo:
        sys.exit("OTA_REPO is empty in include/version.h; set it to owner/name first")
    if not KEY.exists():
        sys.exit(f"No signing key at {KEY}. Run: python tools/ota_keygen.py")
    dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip()
    if dirty and not allow_dirty:
        sys.exit("Uncommitted changes. Commit first so the release matches git, or pass --allow-dirty.")

    out = ROOT / "dist" / f"v{version}"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    print(f"Building {version} for {', '.join(ENVS)}...")
    subprocess.run(["pio", "run"] + sum((["-e", e] for e in ENVS), []), cwd=ROOT, check=True)

    builds = {}
    for env in ENVS:
        name = f"firmware-{env}.bin"
        data = (ROOT / ".pio" / "build" / env / "firmware.bin").read_bytes()
        (out / name).write_bytes(data)
        builds[env] = {"file": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    manifest = json.dumps({"version": version, "builds": builds}, separators=(",", ":"))
    (out / "manifest.json").write_text(manifest, newline="\n")

    ssl = openssl()
    subprocess.run([ssl, "dgst", "-sha256", "-sign", str(KEY), "-out", str(out / "manifest.sig"),
                    str(out / "manifest.json")], check=True)

    # Check it the way the display will: against the public key built into the firmware
    pub_h = (ROOT / "include" / "ota_pubkey.h").read_text()
    pem = "\n".join(re.findall(r'"([^"]*)\\n"', pub_h)) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False) as f:
        f.write(pem)
        pub_file = f.name
    ok = subprocess.run([ssl, "dgst", "-sha256", "-verify", pub_file, "-signature", str(out / "manifest.sig"),
                         str(out / "manifest.json")], capture_output=True, text=True)
    Path(pub_file).unlink()
    if ok.returncode != 0:
        sys.exit("Signature doesn't match include/ota_pubkey.h. Was the key pair changed?")

    print()
    print(f"Release {version} is ready in {out.relative_to(ROOT)}:")
    for p in sorted(out.iterdir()):
        print(f"  {p.name}  ({p.stat().st_size} bytes)")
    print()
    print("Publish it on GitHub, either:")
    print(f"  - Website: https://github.com/{repo}/releases/new  tag v{version}, title v{version},")
    print(f"    attach the four files above, Publish release (not a draft or pre-release).")
    if shutil.which("gh"):
        print(f'  - Or: gh release create v{version} dist/v{version}/* --repo {repo} --title v{version} --notes "..."')
    print("Displays pick it up within a day, or now with Settings > Data source > Check for updates.")


if __name__ == "__main__":
    main()
