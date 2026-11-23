"""Cross-check native ZIP64 output with an independent reader and reopen it."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

exe = str(Path(sys.argv[1]).resolve())
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
with tempfile.TemporaryDirectory(prefix="tapesister-album-") as folder:
    root = Path(folder)
    subprocess.run([exe, "--album-workflow"], cwd=root, env=env, check=True, timeout=90)
    assert not list(root.glob(".tapesister-export-*")), "Export left a private staging directory"
    assert not list(root.glob("*.part-*")), "Export left an incomplete ZIP"
    with zipfile.ZipFile(root / "Unmade-Light.zip") as archive:
        assert archive.testzip() is None
        names = archive.namelist()
        assert "Unmade-Light/Unmade-Light.tsr" in names
        assert any(name.endswith(".wav") for name in names), names
        assert all(name.startswith("Unmade-Light/") and ".." not in Path(name).parts for name in names)
        archive.extractall(root / "reopened")
    subprocess.run([exe, "--open-album", str(root / "reopened/Unmade-Light/Unmade-Light.tsr")],
                   cwd=root, env=env, check=True, timeout=90)
print("Independent ZIP64 CRC/extract and native reopen passed.")
