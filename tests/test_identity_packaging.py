#!/usr/bin/env python3
"""Regression checks for TapeSister identity and portable release wiring."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def check_windows_identity() -> None:
    cmake = read("CMakeLists.txt")
    resource = read("src/tapesister.rc.in")

    assert "configure_file(src/tapesister.rc.in" in cmake
    assert '"${CMAKE_CURRENT_BINARY_DIR}/tapesister.rc"' in cmake
    assert 'OUTPUT_NAME "TapeSister"' in cmake
    assert 'VALUE "ProductName",      "TapeSister\\0"' in resource
    assert 'VALUE "OriginalFilename", "TapeSister.exe\\0"' in resource
    assert 'ICON "@TAPESISTER_WINDOWS_ICON@"' in resource

    icon = ROOT / "assets/icon/tapesister.ico"
    assert icon.is_file() and icon.stat().st_size > 100_000
    for size in (16, 24, 32, 48, 64, 128, 256, 512):
        image = ROOT / f"assets/icon/png/tapesister-{size}.png"
        assert image.is_file() and image.stat().st_size > 0


def check_windows_portable_manifest() -> None:
    stage = read("scripts/stage-windows-portable.ps1")
    package = read("scripts/package-windows-portable.ps1")
    build = read("scripts/build-windows-portable.ps1")
    workflow = read(".github/workflows/native-release-bundles.yml")

    for filename in (
        "TapeSister.exe",
        "SDL2.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
        "libgcc_s_*.dll",
        "tapesister.ini.example",
        "README.txt",
        "THIRD_PARTY_NOTICES.md",
    ):
        assert filename in stage

    for directory in ("assets", "cdp", "licenses"):
        assert f'"{directory}"' in stage

    assert '"TapeSister-Windows-x64"' in package
    assert "Compress-Archive" in package
    assert '$env:MSYSTEM -ne "UCRT64"' in build
    assert '"./build.sh"' in build
    assert "package-windows-portable.ps1" in build
    assert "scripts/package-windows-portable.ps1" in workflow


def main() -> None:
    check_windows_identity()
    check_windows_portable_manifest()
    print("TapeSister identity and portable packaging tests passed.")


if __name__ == "__main__":
    main()
