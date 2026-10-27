import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"


class PackagingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "dist with spaces"
        self.output = Path(self.temp.name) / "packages"
        self.root.mkdir()

    def file(self, path, data=b"test", executable=False):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        if executable:
            target.chmod(0o755)
        return target

    def package(self, platform, version="1.2.3"):
        return subprocess.run([
            sys.executable, str(SCRIPTS / "package.py"), "--root", str(self.root),
            "--output", str(self.output), "--version", version, "--platform", platform,
        ], capture_output=True, text=True)

    def test_tag_must_match_application_version(self):
        environment = os.environ.copy()
        environment.pop("GITHUB_REF_TYPE", None)
        environment.pop("GITHUB_REF_NAME", None)
        environment.pop("GITHUB_OUTPUT", None)
        command = [sys.executable, str(SCRIPTS / "release_version.py")]
        version = subprocess.check_output(command, env=environment, text=True).strip()
        self.assertEqual(subprocess.run(command + [f"v{version}"], env=environment, capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run(command + ["v999.999.999"], env=environment, capture_output=True).returncode, 0)

    def test_linux_archive_runs_after_extracting(self):
        self.file("bin/mapwright", b'#!/bin/sh\nprintf "%s\\n" "$1"\n', executable=True)
        result = self.package("ubuntu-24.04-x86_64")
        self.assertEqual(result.returncode, 0, result.stderr)
        archive = self.output / "MapEditor-1.2.3-ubuntu-24.04-x86_64.tar.gz"
        with tarfile.open(archive) as bundle:
            prefix = "MapEditor-1.2.3-ubuntu-24.04-x86_64"
            self.assertTrue(bundle.getmember(prefix + "/MapEditor.sh").mode & 0o111)
            self.assertTrue(bundle.getmember(prefix + "/bin/mapwright").mode & 0o111)
            bundle.extractall(Path(self.temp.name) / "extracted", filter="data")
        launcher = Path(self.temp.name) / "extracted" / prefix / "MapEditor.sh"
        self.assertEqual(subprocess.check_output([str(launcher), "path with spaces.yaml"], text=True).strip(), "path with spaces.yaml")

    def test_windows_zip_keeps_runtime_and_unicode_files(self):
        self.file("bin/mapwright.exe", b"MZ fixture")
        self.file("bin/Qt6Core.dll")
        self.file("share/mapwright/examples/한글 지도.yaml")
        result = self.package("windows-x86_64")
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.output / "MapEditor-1.2.3-windows-x86_64.zip") as bundle:
            prefix = "MapEditor-1.2.3-windows-x86_64/"
            self.assertIn(prefix + "bin/mapwright.exe", bundle.namelist())
            self.assertIn(prefix + "bin/Qt6Core.dll", bundle.namelist())
            self.assertIn(prefix + "share/mapwright/examples/한글 지도.yaml", bundle.namelist())

    def test_mac_framework_symlink_is_preserved(self):
        self.file("mapwright.app/Contents/MacOS/mapwright", executable=True)
        framework = "mapwright.app/Contents/Frameworks/Test.framework/Versions"
        self.file(framework + "/A/Test")
        (self.root / framework / "Current").symlink_to("A", target_is_directory=True)
        result = self.package("macos-arm64")
        self.assertEqual(result.returncode, 0, result.stderr)
        with tarfile.open(self.output / "MapEditor-1.2.3-macos-arm64.tar.gz") as bundle:
            link = bundle.getmember("MapEditor-1.2.3-macos-arm64/" + framework + "/Current")
            self.assertTrue(link.issym())
            self.assertEqual(link.linkname, "A")

    def test_missing_executable_is_not_packaged(self):
        self.assertNotEqual(self.package("windows-x86_64").returncode, 0)
        self.assertFalse(self.output.exists())

    def test_version_cannot_change_archive_path(self):
        self.file("bin/mapwright.exe")
        self.assertNotEqual(self.package("windows-x86_64", "../../escape").returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
