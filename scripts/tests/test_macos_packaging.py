#!/usr/bin/env python3
"""Portability checks that do not need Homebrew, signing identities or a GUI."""

from pathlib import Path
from argparse import Namespace
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_macos import deployment_versions, native_files, package, resolve_dependency, sign_app, version_tuple


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="atom packaging 原子 ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.app = self.root / "Moved ATOM-STUDIO.app"
        self.executable = self.app / "Contents/MacOS/atom-studio"
        self.executable.parent.mkdir(parents=True)
        self.executable.touch()
        self.library = self.app / "Contents/Frameworks/Example.framework/Versions/A/Example"
        self.library.parent.mkdir(parents=True)
        self.library.touch()

    def test_main_executable_rpaths_resolve_plugin_dependency_after_relocation(self):
        plugin = self.app / "Contents/PlugIns/imageformats/example.dylib"
        resolved = resolve_dependency("@rpath/Example.framework/Versions/A/Example", plugin,
                                      self.executable, [], ["@executable_path/../Frameworks"], self.app)
        self.assertEqual(resolved, self.library)

    def test_missing_library_cannot_fall_back_to_homebrew(self):
        external = self.root / "external"
        external.mkdir()
        (external / "missing.dylib").touch()
        with self.assertRaisesRegex(RuntimeError, "Missing bundled dependency"):
            resolve_dependency("@rpath/missing.dylib", self.executable, self.executable,
                               [str(external)], [], self.app)

    def test_external_absolute_dependency_is_rejected_even_if_it_exists(self):
        with self.assertRaisesRegex(RuntimeError, "Non-relocatable"):
            resolve_dependency(str(self.library), self.executable, self.executable, [], [], self.app)

    def test_system_shared_cache_library_does_not_need_a_file(self):
        self.assertIsNone(resolve_dependency("/usr/lib/libSystem.B.dylib", self.executable,
                                             self.executable, [], [], self.app))

    def test_symlink_cannot_escape_bundle(self):
        (self.app / "external").symlink_to(self.root)
        with self.assertRaisesRegex(RuntimeError, "external symlink"):
            native_files(self.app)

    def test_broken_internal_symlink_is_rejected(self):
        (self.app / "missing").symlink_to("missing-target")
        with self.assertRaisesRegex(RuntimeError, "Broken"):
            native_files(self.app)

    def test_dependency_symlink_cannot_escape_bundle(self):
        outside = self.root / "outside.dylib"
        outside.touch()
        (self.app / "Contents/Frameworks/escape.dylib").symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError, "Missing bundled dependency"):
            resolve_dependency("@executable_path/../Frameworks/escape.dylib", self.executable,
                               self.executable, [], [], self.app)

    def test_minimum_os_does_not_use_library_or_sdk_versions(self):
        commands = """
 cmd LC_SOURCE_VERSION
 version 999.0
 cmd LC_BUILD_VERSION
 platform 1
 minos 14.0
 sdk 26.2
 cmd LC_VERSION_MIN_MACOSX
 version 11.0
 sdk 12.3
"""
        self.assertEqual(deployment_versions(commands), ["14.0", "11.0"])
        self.assertEqual(version_tuple("14"), version_tuple("14.0.0"))
        self.assertLess(version_tuple("14.9"), version_tuple("14.10"))

    @unittest.skipUnless(sys.platform == "darwin", "Uses macOS ditto")
    def test_zero_exit_qt_error_preserves_previous_disk_image(self):
        (self.app / "Contents/Resources/python").mkdir(parents=True)
        (self.app / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleShortVersionString": "0.1.0", "CFBundleExecutable": "atom-studio"}))
        deploy = self.root / "macdeployqt"
        deploy.write_text("#!/bin/sh\nprintf 'ERROR: Cannot resolve a required framework\\n'\nexit 0\n")
        deploy.chmod(0o755)
        output = self.root / "output"
        output.mkdir()
        image = output / "ATOM-STUDIO-0.1.0-macOS-arm64.dmg"
        image.write_bytes(b"previous successful build")
        args = Namespace(app=self.app, output_dir=output, macdeployqt=deploy,
                         qml_dir=self.root, qml_import_dir=self.root, library_dir=self.root,
                         plugin_dir=[], architecture="arm64", minimum_macos="14.0")
        with patch("package_macos.load_commands", return_value=([], [str(self.root)], False)):
            with self.assertRaisesRegex(RuntimeError, "Qt deployment failed"):
                package(args)
        self.assertEqual(image.read_bytes(), b"previous successful build")
        self.assertEqual(list(output.glob(".package-*")), [])

    @unittest.skipUnless(sys.platform == "darwin", "Uses Apple compiler and codesign")
    def test_local_signing_seals_an_app_with_a_native_plugin(self):
        app = self.root / "Signing Test.app"
        executable = app / "Contents/MacOS/check"
        plugin = app / "Contents/PlugIns/example.dylib"
        executable.parent.mkdir(parents=True)
        plugin.parent.mkdir(parents=True)
        (app / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "check", "CFBundleIdentifier": "com.atomstudio.signing-test",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1"}))
        subprocess.run(["/usr/bin/xcrun", "clang", "-x", "c", "-", "-o", str(executable)],
                       input="int main(void) { return 0; }", text=True, check=True, capture_output=True)
        subprocess.run(["/usr/bin/xcrun", "clang", "-dynamiclib", "-x", "c", "-", "-o", str(plugin)],
                       input="int answer(void) { return 42; }", text=True, check=True, capture_output=True)
        sign_app(app, [executable, plugin])
        subprocess.run([str(executable)], check=True)
        # Changing a nested plugin must invalidate the outer app's resource seal.
        with plugin.open("ab") as stream:
            stream.write(b"modified")
        result = subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)],
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
