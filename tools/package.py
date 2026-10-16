"""Package a Chiller build into a release zip.

  python tools/package.py --platform windows|macos|linux --version 0.1.0 --artefacts build/Chiller_artefacts/Release --out dist

Output: dist/Chiller-<version>-<win64|macos|linux>.zip
Unix file modes are preserved in the zip, so macOS bundles and the Linux standalone keep their executable bits.
"""
import argparse
import os
import stat
import zipfile

REPO = "https://github.com/huuba91/chiller"

ABLETON = """ABLETON LIVE
  Preferences > Plug-ins > enable "Use VST3 Plug-in System Folders" (and "Use Audio Units" on macOS),
  then click "Rescan". Chiller appears under Plug-ins > Effects (look for "Chiller", manufacturer Neuroforge).
  Drop it on a track. Use the Mix mode for tracks and buses, Master for the final bounce.
  Sidechain key: pick a source track in the Audio From chooser of Chiller's sidechain section, then switch
  "Sidechain key" on in the Explain view.
"""

WINDOWS_INSTALL = """CHILLER {version} (Windows x64) - alpha
======================================

VST3  (Ableton Live, Cubase, FL Studio, Studio One, Reaper, ...)
  Copy the folder "Chiller.vst3" to:  C:\\Program Files\\Common Files\\VST3\\
  (needs administrator rights; the whole "Chiller.vst3" folder must be copied, not only the file inside it)

CLAP  (Bitwig, Reaper, FL Studio, Studio One, ...)
  Copy the file "Chiller.clap" to:    C:\\Program Files\\Common Files\\CLAP\\

Then rescan plug-ins in your DAW. Chiller appears as an effect.

""" + ABLETON + """
Standalone
  Run Chiller.exe (choose your audio device under Options).

Alpha: the suppression engine works, but expect rough edges. See docs/release-notes/v0.1.0-alpha.md
Roadmap: {repo}/blob/main/docs/PLAN.md
Bugs & feedback:    {repo}/issues

The binaries are not code-signed; Windows SmartScreen may warn ("More info" > "Run anyway").
Licensed under the GNU AGPLv3 (see LICENSE.txt). Source: {repo}
Third-party licenses are in the "third-party-licenses" folder.
"""

MACOS_INSTALL = """CHILLER {version} (macOS 10.15+, Apple Silicon + Intel) - alpha
==============================================================

EASIEST: one line in Terminal installs everything and avoids the macOS warnings:

  curl -fsSL https://raw.githubusercontent.com/huuba91/chiller/main/install-mac.sh | bash

MANUAL INSTALL (from this zip)
  Copy each format to its folder (in Finder: Go > Go to Folder...):
    Chiller.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/        (Ableton, Bitwig, Reaper, FL, Cubase, ...)
    Chiller.component  ->  ~/Library/Audio/Plug-Ins/Components/  (Logic Pro, GarageBand, Ableton via Audio Units)
    Chiller.clap       ->  ~/Library/Audio/Plug-Ins/CLAP/        (Bitwig, Reaper, ...)
    Chiller.app        ->  /Applications or ~/Applications       (standalone)

  These builds are not notarized by Apple, so macOS blocks them after a browser download.
  Run this once in Terminal after copying, then restart your DAW:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/*/Chiller.* ~/Applications/Chiller.app /Applications/Chiller.app 2>/dev/null; killall -9 AudioComponentRegistrar 2>/dev/null; true

""" + ABLETON + """
Alpha: the suppression engine works, but expect rough edges. See docs/release-notes/v0.1.0-alpha.md
Roadmap: {repo}/blob/main/docs/PLAN.md
Bugs & feedback:    {repo}/issues

Licensed under the GNU AGPLv3 (see LICENSE.txt). Source: {repo}
Third-party licenses are in the "third-party-licenses" folder.
"""

LINUX_INSTALL = """CHILLER {version} (Linux x86_64) - alpha
========================================

Copy each format to your plug-in folder, then rescan plug-ins in your DAW:
  Chiller.vst3  ->  ~/.vst3/
  Chiller.clap  ->  ~/.clap/
  Chiller.lv2   ->  ~/.lv2/
  Chiller       ->  anywhere (standalone app: chmod +x Chiller)

Alpha: the suppression engine works, but expect rough edges. See docs/release-notes/v0.1.0-alpha.md
Roadmap: {repo}/blob/main/docs/PLAN.md
Bugs & feedback:    {repo}/issues

Licensed under the GNU AGPLv3 (see LICENSE.txt). Source: {repo}
Third-party licenses are in the "third-party-licenses" folder.
"""


def add_path(z: zipfile.ZipFile, src: str, arc: str) -> None:
    """Add a file or directory tree, preserving unix permissions and symlinks."""
    if os.path.isdir(src) and not os.path.islink(src):
        for name in sorted(os.listdir(src)):
            add_path(z, os.path.join(src, name), f"{arc}/{name}")
        return
    st = os.lstat(src)
    info = zipfile.ZipInfo(arc)
    info.compress_type = zipfile.ZIP_DEFLATED
    if os.path.islink(src):
        info.external_attr = (stat.S_IFLNK | 0o755) << 16
        z.writestr(info, os.readlink(src))
    else:
        info.external_attr = (stat.S_IFREG | (st.st_mode & 0o777 or 0o644)) << 16
        with open(src, "rb") as f:
            z.writestr(info, f.read())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", choices=["windows", "macos", "linux"], required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--artefacts", required=True, help="JUCE artefacts dir for the Release config")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--cje", default="libs/clap-juce-extensions")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    suffix = {"windows": "win64", "macos": "macos", "linux": "linux"}[a.platform]
    out = os.path.join(a.out, f"Chiller-{a.version}-{suffix}.zip")
    art = a.artefacts

    if a.platform == "windows":
        items = [("VST3/Chiller.vst3", "Chiller.vst3"), ("CLAP/Chiller.clap", "Chiller.clap"),
                 ("Standalone/Chiller.exe", "Chiller.exe")]
        install = WINDOWS_INSTALL.format(version=a.version, repo=REPO).replace("\n", "\r\n")
    elif a.platform == "macos":
        items = [("VST3/Chiller.vst3", "Chiller.vst3"), ("AU/Chiller.component", "Chiller.component"),
                 ("CLAP/Chiller.clap", "Chiller.clap"), ("Standalone/Chiller.app", "Chiller.app")]
        install = MACOS_INSTALL.format(version=a.version, repo=REPO)
    else:
        items = [("VST3/Chiller.vst3", "Chiller.vst3"), ("CLAP/Chiller.clap", "Chiller.clap"),
                 ("LV2/Chiller.lv2", "Chiller.lv2"), ("Standalone/Chiller", "Chiller")]
        install = LINUX_INSTALL.format(version=a.version, repo=REPO)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in items:
            path = os.path.join(art, src)
            if not os.path.exists(path):
                raise SystemExit(f"missing build output: {path}")
            add_path(z, path, arc)
        info = zipfile.ZipInfo("INSTALL.txt")
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (stat.S_IFREG | 0o644) << 16
        z.writestr(info, install)
        z.write("LICENSE", "LICENSE.txt")
        z.write("assets/fonts/LICENSE-Apache-2.0.txt", "third-party-licenses/RobotoCondensed-LICENSE-Apache-2.0.txt")
        z.write(f"{a.cje}/LICENSE.md", "third-party-licenses/clap-juce-extensions-LICENSE.txt")
        z.write(f"{a.cje}/clap-libs/clap/LICENSE", "third-party-licenses/clap-LICENSE.txt")
        z.write(f"{a.cje}/clap-libs/clap-helpers/LICENSE", "third-party-licenses/clap-helpers-LICENSE.txt")
    print(out, os.path.getsize(out) // 1024, "KB")


if __name__ == "__main__":
    main()
