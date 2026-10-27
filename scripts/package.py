"""Package an installed desktop bundle without losing Unix modes or Qt symlinks."""
import argparse
from pathlib import Path
import re
import tarfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("dist"))
    parser.add_argument("--output", type=Path, default=Path("packages"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True, choices=(
        "ubuntu-24.04-x86_64", "windows-x86_64", "macos-arm64", "macos-x86_64"
    ))
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("version must use X.Y.Z")
    root = args.root.resolve()
    windows = args.platform.startswith("windows")
    mac = args.platform.startswith("macos")
    executable = root / ("mapwright.app/Contents/MacOS/mapwright" if mac else
                         "bin/mapwright.exe" if windows else "bin/mapwright")
    if not executable.is_file():
        parser.error(f"installed application is missing: {executable}")
    if windows:
        launch = "bin/mapwright.exe를 실행하세요."
    elif mac:
        launch = "mapwright.app을 여세요."
    else:
        launcher = root / "MapEditor.sh"
        launcher.write_text('#!/usr/bin/env sh\nset -eu\n'
                            'mapeditor_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                            'exec "$mapeditor_dir/bin/mapwright" "$@"\n', encoding="utf-8")
        launcher.chmod(0o755)
        launch = "터미널에서 ./MapEditor.sh를 실행하세요."
    (root / "START-HERE.txt").write_text(
        f"Map Editor {args.version}\n{args.platform}\n\n{launch}\n\n"
        "사용법: share/mapwright/README.md\n"
        "예제 지도: share/mapwright/examples/office.yaml\n"
        "라이선스: share/mapwright/LICENSE, share/mapwright/licenses/\n",
        encoding="utf-8",
    )
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"MapEditor-{args.version}-{args.platform}"
    if windows:
        archive = args.output / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    bundle.write(path, (Path(name) / path.relative_to(root)).as_posix())
    else:
        archive = args.output / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz", dereference=False) as bundle:
            bundle.add(root, arcname=name)
    print(archive)


if __name__ == "__main__":
    main()
