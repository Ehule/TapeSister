#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir"

case "$(uname -s)" in
  Linux*)
    platform="Linux"
    build_dir="${TAPESISTER_BUILD_DIR:-$script_dir/build-linux}"
    generator_args=()
    executable="$build_dir/tapesister"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
      printf 'TapeSister Windows builds require an MSYS2 UCRT64 terminal.\n' >&2
      printf 'Open "MSYS2 UCRT64", return to this folder, and run: bash build.sh\n' >&2
      exit 1
    fi
    platform="Windows UCRT64"
    build_dir="${TAPESISTER_BUILD_DIR:-$script_dir/build-windows}"
    generator_args=(-G Ninja)
    executable="$build_dir/tapesister.exe"
    ;;
  *)
    printf 'Unsupported build host: %s\n' "$(uname -s)" >&2
    exit 1
    ;;
esac

for command_name in cmake git cc c++; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'Missing required build command: %s\n' "$command_name" >&2
    if [[ "$platform" == "Linux" ]]; then
      printf 'On Debian/Ubuntu: sudo apt install build-essential cmake git libsdl2-dev libasound2-dev\n' >&2
    else
      printf 'In UCRT64, install: git mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2\n' >&2
    fi
    exit 1
  fi
done

if [[ "$platform" == "Windows UCRT64" ]] && ! command -v ninja >/dev/null 2>&1; then
  printf 'Missing required Windows build command: ninja\n' >&2
  printf 'Install mingw-w64-ucrt-x86_64-ninja from the MSYS2 UCRT64 terminal.\n' >&2
  exit 1
fi

build_jobs="${TAPESISTER_BUILD_JOBS:-2}"
if [[ ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
  printf 'TAPESISTER_BUILD_JOBS must be a positive integer, not: %s\n' "$build_jobs" >&2
  exit 1
fi

printf 'Configuring the complete TapeSister + CDP8 %s build...\n' "$platform"
cmake -S "$script_dir" -B "$build_dir" "${generator_args[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTAPESISTER_BUNDLE_CDP8=ON

printf 'Building TapeSister and the pinned native CDP8 runtime...\n'
cmake --build "$build_dir" --target tapesister --parallel "$build_jobs"

if [[ ! -x "$executable" ]]; then
  printf 'Build completed without producing the expected executable: %s\n' "$executable" >&2
  exit 1
fi
if [[ ! -d "$build_dir/cdp/bin" ]]; then
  printf 'Build completed without staging the required CDP8 runtime: %s\n' "$build_dir/cdp/bin" >&2
  exit 1
fi

printf '\nComplete build ready:\n  %s\n' "$executable"
printf 'Launch it with:\n  '
printf '%q' "$executable"
printf '\n'
