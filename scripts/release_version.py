"""Read the application version and reject mismatched release tags."""
import os
from pathlib import Path
import re
import sys


def main():
    source = Path(__file__).resolve().parents[1] / "CMakeLists.txt"
    match = re.search(r"project\(Mapwright\s+VERSION\s+(\d+\.\d+\.\d+)\b", source.read_text())
    if not match:
        raise SystemExit("Cannot read project VERSION from CMakeLists.txt")
    version = match.group(1)
    tag = sys.argv[1] if len(sys.argv) == 2 else (
        os.environ.get("GITHUB_REF_NAME") if os.environ.get("GITHUB_REF_TYPE") == "tag" else None
    )
    if tag and tag != f"v{version}":
        raise SystemExit(f"Release tag {tag!r} must match CMake version v{version}")
    if output := os.environ.get("GITHUB_OUTPUT"):
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"version={version}\n")
    print(version)


if __name__ == "__main__":
    main()
