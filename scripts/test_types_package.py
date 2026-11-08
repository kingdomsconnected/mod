"""Check the notices shipped by the types package without publishing it."""

import json
import os
import pathlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest


class LicenseCopiesTests(unittest.TestCase):
    def test_local_packages_carry_the_current_license_and_notice(self):
        repository = pathlib.Path(__file__).resolve().parents[1]
        for name in ["scripting-api", "resources/kcdc-gamemode", "resources/optional/kcdc-playground", "ui"]:
            with self.subTest(package=name):
                package = repository / name
                for notice in ["LICENSE.txt", "NOTICE.txt"]:
                    self.assertEqual((package / notice).read_bytes(), (repository / notice).read_bytes())
                manifest = json.loads((package / "package.json").read_text())
                self.assertEqual(manifest["license"], "SEE LICENSE IN LICENSE.txt")


@unittest.skipUnless(shutil.which("node") and shutil.which("npm"), "Node and npm are required")
class TypesPackageTests(unittest.TestCase):
    def setUp(self):
        repository = pathlib.Path(__file__).resolve().parents[1]
        self.temporary = tempfile.TemporaryDirectory(dir=repository)
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        shutil.copyfile(repository / "scripts/build_types_package.mjs", self.root / "scripts/build_types_package.mjs")
        self.api = self.root / "scripting-api"
        (self.api / "generated").mkdir(parents=True)
        (self.api / "package").mkdir()
        self.notices = {"LICENSE.txt": b"Package license\r\n", "NOTICE.txt": b"Package attribution\n"}
        for name, contents in self.notices.items():
            (self.root / name).write_bytes(contents)
            (self.api / name).write_text("STALE package copy")
        files = ["shared.d.ts", "generated/server-api.d.ts", "generated/client-api.d.ts"]
        for name in files:
            (self.api / name).write_text("export {};\n")
        (self.api / "package/README.md").write_text("Package readme\n")
        (self.api / "private.txt").write_text("PRIVATE")
        (self.api / "package.json").write_text(json.dumps({"name": "@kingdomsconnected/types", "version": "0.0.0", "files": files + list(self.notices)}))
        self.output = self.root / "package"

    def build(self):
        return subprocess.run(["node", str(self.root / "scripts/build_types_package.mjs"), "--out", str(self.output), "--version", "1.0.0-test"], capture_output=True, text=True)

    def test_package_and_tarball_preserve_the_root_notices(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        for name, contents in self.notices.items():
            self.assertEqual((self.output / name).read_bytes(), contents)
        manifest = json.loads((self.output / "package.json").read_text())
        self.assertTrue(set(self.notices).issubset(manifest["files"]))
        self.assertFalse((self.output / "private.txt").exists())
        environment = os.environ | {"npm_config_update_notifier": "false"}
        packed = subprocess.run(["npm", "pack", str(self.output), "--json", "--offline", "--ignore-scripts", "--cache", str(self.root / "cache"), "--pack-destination", str(self.root)], capture_output=True, text=True, env=environment)
        self.assertEqual(packed.returncode, 0, packed.stderr)
        filename = json.loads(packed.stdout)[0]["filename"]
        with tarfile.open(self.root / filename) as archive:
            for name, contents in self.notices.items():
                self.assertEqual(archive.extractfile(f"package/{name}").read(), contents)
            self.assertNotIn("package/private.txt", archive.getnames())

    def test_missing_root_notice_is_rejected(self):
        for name, contents in self.notices.items():
            with self.subTest(name=name):
                (self.root / name).unlink()
                result = self.build()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(name, result.stderr)
                (self.root / name).write_bytes(contents)


if __name__ == "__main__":
    unittest.main()
