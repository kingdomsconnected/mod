"""Regression checks for the public image's private-artifact boundary."""

import pathlib
import shutil
import tempfile
import unittest
from unittest import mock

import stage_server_image


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        self.destination = self.root / "context"
        for name in ["KCDCServer", "crashpad_handler", "libnode.so.141"]:
            (self.bundle / name).write_bytes(b"\x7fELF\x02" + b"runtime")
        for name in stage_server_image.NOTICES:
            stage_server_image.ensure_parent(self.bundle / name).write_bytes(f"Bundled {name}\r\n".encode("utf-8"))
        self.sections = mock.patch.object(stage_server_image.subprocess, "check_output", return_value=".text .dynsym .note.gnu.build-id")
        self.strip = mock.patch.object(stage_server_image.subprocess, "run")
        self.sections.start()
        self.strip_mock = self.strip.start()
        self.addCleanup(self.sections.stop)
        self.addCleanup(self.strip.stop)
        self.seed_default_resources()

    def seed_default_resources(self):
        """The shape the Linux server bundle carries: the resource as source."""
        for name in stage_server_image.DEFAULT_RESOURCES:
            resource = self.bundle / "resources" / name
            (resource / "src/server/commands").mkdir(parents=True)
            (resource / "src/client").mkdir(parents=True)
            (resource / "package.json").write_text('{"name": "%s"}' % name)
            (resource / "package-lock.json").write_text('{"lockfileVersion": 3}')
            (resource / "README.md").write_text("read me")
            (resource / "tsconfig.json").write_text('{"compilerOptions": {}}')
            for notice in ["LICENSE.txt", "NOTICE.txt"]:
                (resource / notice).write_text(f"Resource {notice}")
            (resource / "src/server/index.ts").write_text("// entry point")
            (resource / "src/server/commands/horse.ts").write_text("// one command")
            (resource / "src/client/tsconfig.json").write_text('{"compilerOptions": {}}')

    def test_private_artifacts_never_enter_context(self):
        for name in ["source.cpp", "server.json", ".env", "KCDCServer.pdb", "libnode.so.private.js", "licenses/private.txt"]:
            (self.bundle / name).write_text("PRIVATE")
        # Beside the default resource and not one of them: only named
        # directories are taken out of resources/.
        (self.bundle / "resources/main.js").write_text("PRIVATE")
        (self.bundle / "resources/operator-gamemode").mkdir()
        (self.bundle / "resources/operator-gamemode/server.js").write_text("PRIVATE")
        (self.bundle / ".git").mkdir()
        (self.bundle / ".git/config").write_text("PRIVATE")
        stage_server_image.stage(self.bundle, self.destination)
        staged = {str(p.relative_to(self.destination)).replace("\\", "/"): p for p in self.destination.rglob("*") if p.is_file()}
        expected = {"Dockerfile", ".dockerignore", "entrypoint.sh", "egg-kcdc.json", "runtime/KCDCServer", "runtime/crashpad_handler", "runtime/libnode.so.141"}
        expected |= {f"runtime/{name}" for name in stage_server_image.NOTICES}
        for name in stage_server_image.DEFAULT_RESOURCES:
            expected |= {f"default-resources/{name}/{f}" for f in ["package.json", "package-lock.json", "README.md", "tsconfig.json", "LICENSE.txt", "NOTICE.txt", "src/server/index.ts", "src/server/commands/horse.ts", "src/client/tsconfig.json"]}
        self.assertEqual(set(staged), expected)
        # Names alone would miss a private file copied under a plausible one.
        for name, path in staged.items():
            if name not in {"runtime/KCDCServer", "runtime/crashpad_handler", "runtime/libnode.so.141"}:
                self.assertNotIn("PRIVATE", path.read_text(encoding="utf-8"), name)
        self.assertEqual((self.bundle / "KCDCServer").read_bytes(), (self.destination / "runtime/KCDCServer").read_bytes())
        self.assertFalse(any("KCDCServer" in str(call) for call in self.strip_mock.call_args_list))
        self.assertNotIn(b"\r", (self.destination / "entrypoint.sh").read_bytes())

    def test_notices_are_preserved_from_the_tested_bundle(self):
        stage_server_image.stage(self.bundle, self.destination)
        for name in stage_server_image.NOTICES:
            self.assertEqual((self.destination / "runtime" / name).read_bytes(), (self.bundle / name).read_bytes())

    def test_missing_notice_is_rejected(self):
        for name in stage_server_image.NOTICES:
            with self.subTest(name=name):
                source = self.bundle / name
                contents = source.read_bytes()
                source.unlink()
                with self.assertRaisesRegex(ValueError, "Required notice"):
                    stage_server_image.stage(self.bundle, self.root / ("missing-" + pathlib.Path(name).name))
                source.write_bytes(contents)

    def test_notice_symlink_cannot_escape_bundle(self):
        target = self.root / "private-notice"
        target.write_text("PRIVATE")
        source = self.bundle / "NOTICE.txt"
        source.unlink()
        try:
            source.symlink_to(target)
        except OSError:
            self.skipTest("Creating symlinks requires host privileges")
        with self.assertRaisesRegex(ValueError, "Required notice"):
            stage_server_image.stage(self.bundle, self.destination)

    def test_default_gamemode_is_staged_from_the_bundle(self):
        for name in stage_server_image.DEFAULT_RESOURCES:
            # An emit and a toolchain, which the bundle never carries and the
            # image must not either even when a local tree has them.
            (self.bundle / "resources" / name / "dist/server").mkdir(parents=True)
            (self.bundle / "resources" / name / "dist/server/index.js").write_text("PRIVATE")
            (self.bundle / "resources" / name / "node_modules").mkdir()
            (self.bundle / "resources" / name / "node_modules/typescript.js").write_text("PRIVATE")

        stage_server_image.stage(self.bundle, self.destination)

        for name in stage_server_image.DEFAULT_RESOURCES:
            staged = self.destination / "default-resources" / name
            self.assertEqual((staged / "package.json").read_text(), '{"name": "%s"}' % name)
            self.assertTrue((staged / "src/server/index.ts").is_file())
            self.assertTrue((staged / "src/server/commands/horse.ts").is_file())
            self.assertTrue((staged / "tsconfig.json").is_file())
            self.assertTrue((staged / "package-lock.json").is_file())
            for notice in ["LICENSE.txt", "NOTICE.txt"]:
                self.assertEqual((staged / notice).read_bytes(), (self.bundle / "resources" / name / notice).read_bytes())
            self.assertFalse((staged / "dist").exists())
            self.assertFalse((staged / "node_modules").exists())

    def test_a_bundle_without_the_default_gamemode_is_rejected(self):
        shutil.rmtree(self.bundle / "resources" / stage_server_image.DEFAULT_RESOURCES[0])
        with self.assertRaisesRegex(ValueError, "missing from the bundle"):
            stage_server_image.stage(self.bundle, self.destination)

    def test_a_default_gamemode_with_no_sources_is_rejected(self):
        shutil.rmtree(self.bundle / "resources" / stage_server_image.DEFAULT_RESOURCES[0] / "src")
        with self.assertRaisesRegex(ValueError, "no TypeScript"):
            stage_server_image.stage(self.bundle, self.destination)

    def test_existing_context_is_rejected(self):
        self.destination.mkdir()
        (self.destination / "secret").write_text("PRIVATE")
        with self.assertRaises(FileExistsError):
            stage_server_image.stage(self.bundle, self.destination)

    def test_missing_node_is_rejected(self):
        (self.bundle / "libnode.so.141").unlink()
        with self.assertRaisesRegex(ValueError, "missing libnode"):
            stage_server_image.stage(self.bundle, self.destination)

    def test_non_elf_is_rejected(self):
        (self.bundle / "KCDCServer").write_text("PRIVATE source masquerading as a binary")
        with self.assertRaisesRegex(ValueError, "64-bit ELF"):
            stage_server_image.stage(self.bundle, self.destination)

    def test_debug_sections_are_rejected(self):
        for section in [".debug_info", ".zdebug_info", ".symtab", ".gdb_index", ".gnu_debuglink"]:
            with self.subTest(section=section), mock.patch.object(stage_server_image.subprocess, "check_output", return_value=section):
                with self.assertRaisesRegex(ValueError, "Debug information"):
                    stage_server_image.verify_elf(self.bundle / "KCDCServer")

    def test_symlink_cannot_escape_bundle(self):
        target = self.root / "private"
        target.write_bytes(b"\x7fELF\x02PRIVATE")
        (self.bundle / "KCDCServer").unlink()
        try:
            (self.bundle / "KCDCServer").symlink_to(target)
        except OSError:
            self.skipTest("Creating symlinks requires host privileges")
        with self.assertRaisesRegex(ValueError, "inside the bundle"):
            stage_server_image.stage(self.bundle, self.destination)


if __name__ == "__main__":
    unittest.main()
