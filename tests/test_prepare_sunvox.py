"""Regression for native Windows Python selecting a legacy default encoding."""
import hashlib
import io
from pathlib import Path
import runpy
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "tools/prepare_sunvox_realtime.py"
ARCHIVE = REPO / "third_party/sunvox/sunvox-2.1.4d.tar.gz"
# Known-good output from the audio-validated SDK preparation.
# These hashes cover whole files, including their non-ASCII source strings.
PREPARED = {
    "lib_sunvox/sunvox_engine.cpp": "02e10a346b1274ec81548bf86021571136b23633483374f88fdcd3c0e60faffd",
    "lib_sunvox/psynth/psynths_reverb.cpp": "6fe70d983acb191bdb5719d2876272c8396fecd8a39faf2705cf128cbbcdea90",
    "lib_sunvox/psynth/psynths_delay.cpp": "4095544f408df585243a11f02af66f371cfb648c6dc96ff8a441f0a1f2667c36",
}
ORIGINAL_OPEN = io.open


def cp1252_text_encoding(encoding, stacklevel=2):
    # pathlib resolves None before calling io.open, including in UTF-8 mode.
    return "cp1252" if encoding in (None, "locale") else encoding


def cp1252_open(file, mode="r", buffering=-1, encoding=None, errors=None,
                newline=None, closefd=True, opener=None):
    # Model native Windows text I/O on every test platform. Explicit encodings
    # and binary I/O retain their normal behavior; implicit text I/O uses CP1252.
    if "b" not in mode and encoding in (None, "locale"):
        encoding = "cp1252"
    return ORIGINAL_OPEN(file, mode, buffering, encoding, errors, newline, closefd, opener)


class PrepareSunVoxTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sunvox-preparation-")
        self.addCleanup(self.temp.cleanup)
        # Paths must work independently of the content's text encoding too.
        self.root = Path(self.temp.name) / "patch space \u00e9"
        with tarfile.open(ARCHIVE, "r:gz") as archive:
            for name in PREPARED:
                path = self.root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(name) as source:
                    path.write_bytes(source.read())

    def prepare_and_check(self):
        with patch.object(sys, "argv", [str(SCRIPT), str(self.root)]):
            runpy.run_path(str(SCRIPT), run_name="__main__")
        for name, expected in PREPARED.items():
            with self.subTest(source=name):
                self.assertEqual(hashlib.sha256((self.root / name).read_bytes()).hexdigest(), expected)

    def test_native_default(self):
        self.prepare_and_check()

    def test_cp1252_default(self):
        # This is the actual pinned file and the byte that broke the user's build.
        reverb = self.root / "lib_sunvox/psynth/psynths_reverb.cpp"
        original = reverb.read_bytes()
        original.decode("utf-8")
        with self.assertRaises(UnicodeDecodeError):
            original.decode("cp1252")
        with patch.object(io, "text_encoding", cp1252_text_encoding), patch.object(io, "open", cp1252_open):
            self.prepare_and_check()

    def test_changed_source_is_rejected(self):
        path = self.root / "lib_sunvox/sunvox_engine.cpp"
        path.write_bytes(path.read_bytes().replace(b"    s->stream_control = stream_control;", b""))
        with self.assertRaisesRegex(SystemExit, "Unexpected pinned SunVox source"):
            self.prepare_and_check()

    def test_changed_delay_is_rejected(self):
        path = self.root / "lib_sunvox/psynth/psynths_delay.cpp"
        path.write_bytes(path.read_bytes().replace(b"if( data->ctl_inverse ) ctl_wet = -ctl_wet;", b""))
        with self.assertRaisesRegex(SystemExit, "Unexpected pinned SunVox source"):
            self.prepare_and_check()

    def test_reference_is_pristine(self):
        self.prepare_and_check()
        with tarfile.open(ARCHIVE, "r:gz") as archive:
            with archive.extractfile("lib_sunvox/psynth/psynths_delay.cpp") as source:
                self.assertEqual((self.root / "reference/psynths_delay.cpp").read_bytes(), source.read())


if __name__ == "__main__":
    unittest.main()
