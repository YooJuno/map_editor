"""Check the installed application's OS icon wiring, without launching it."""
import argparse
import ctypes
from pathlib import Path
import plistlib
import struct
import subprocess
import tempfile


def check(root, platform):
    resources = Path(__file__).resolve().parents[1] / "resources"
    if platform.startswith("macos"):
        bundle = root / "mapwright.app/Contents"
        with (bundle / "Info.plist").open("rb") as source:
            metadata = plistlib.load(source)
        icon = bundle / "Resources" / metadata["CFBundleIconFile"]
        if icon.read_bytes() != (resources / "mapwright.icns").read_bytes():
            raise ValueError("The app bundle does not contain the current icon")
        # Ask macOS itself to decode every representation in the ICNS file.
        with tempfile.TemporaryDirectory(prefix="mapeditor-icon-") as directory:
            output = Path(directory) / "MapEditor.iconset"
            subprocess.run(["iconutil", "--convert", "iconset", str(icon), "--output", str(output)], check=True)
            if not (output / "icon_512x512@2x.png").is_file():
                raise ValueError("The macOS icon is missing its 1024 px representation")
    elif platform.startswith("windows"):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
        kernel.LoadLibraryExW.restype = ctypes.c_void_p
        kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        kernel.FindResourceW.restype = ctypes.c_void_p
        kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel.LoadResource.restype = ctypes.c_void_p
        kernel.LockResource.argtypes = [ctypes.c_void_p]
        kernel.LockResource.restype = ctypes.c_void_p
        kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel.SizeofResource.restype = ctypes.c_uint32
        kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
        module = kernel.LoadLibraryExW(str(root / "bin/mapwright.exe"), None, 2)  # Data only; do not execute.
        if not module:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            group = kernel.FindResourceW(module, ctypes.c_void_p(101), ctypes.c_void_p(14))
            if not group:
                raise ValueError("Windows executable is missing its application icon resource")
            data = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, group)), kernel.SizeofResource(module, group))
            count = struct.unpack_from("<H", data, 4)[0]
            sizes = {data[6 + index * 14] or 256 for index in range(count)}
            if not {16, 32, 48, 256}.issubset(sizes):
                raise ValueError(f"Windows icon representations are incomplete: {sizes}")
        finally:
            kernel.FreeLibrary(module)
    else:
        theme = root / "share/icons/hicolor"
        for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
            icon = theme / f"{size}x{size}/apps/mapwright.png"
            source = resources / "mapwright.png" if size == 128 else resources / f"icons/mapwright-{size}.png"
            if icon.read_bytes() != source.read_bytes():
                raise ValueError(f"Installed Linux icon is stale: {size}")
        if (theme / "scalable/apps/mapwright.svg").read_bytes() != (resources / "mapwright.svg").read_bytes():
            raise ValueError("Scalable Linux icon is missing")
        desktop = (root / "share/applications/mapwright.desktop").read_text(encoding="utf-8")
        if "Icon=mapwright\n" not in desktop or "Exec=mapwright %f\n" not in desktop:
            raise ValueError("Linux application entry does not match the executable and icon")
    print(f"Native application icon verified: {platform}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("dist"))
    parser.add_argument("--platform", required=True)
    args = parser.parse_args()
    check(args.root.resolve(), args.platform)


if __name__ == "__main__":
    main()
