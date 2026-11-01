"""Builds and runs the host test of the climate layout and codes (run: pytest test/):
test/climate/climate_codes_test.cpp against components/canopen_entities/climate_codes.cpp. Also checks the C++
layout against layout.py."""

import importlib.util
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).parent.parent
MAPPER = ROOT / "components" / "canopen_entities"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]

_spec = importlib.util.spec_from_file_location("climate_layout", MAPPER / "layout.py")
layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(layout)


def _compile(cxx: str, out: Path, extra: list[str]) -> subprocess.CompletedProcess:
    cmd = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", *extra, f"-I{MAPPER}"]
    cmd += [str(ROOT / "test" / "climate" / "climate_codes_test.cpp"), str(MAPPER / "climate_codes.cpp")]
    return subprocess.run([*cmd, "-o", str(out)], capture_output=True, text=True, check=False)


def test_climate_codes_cpp(tmp_path: Path) -> None:
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        pytest.skip("no C++ compiler")
    exe = tmp_path / "climate_codes_test"
    result = _compile(cxx, exe, SANITIZERS)
    if result.returncode != 0 and "sanitize" in result.stderr:
        result = _compile(cxx, exe, [])  # toolchain without sanitizer runtimes
    assert result.returncode == 0, result.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout.strip() == "OK"


def test_constants_match_layout() -> None:
    """The constants climate_codes.h shares with layout.py."""
    header = (MAPPER / "climate_codes.h").read_text(encoding="utf-8")
    for name, value in (
        ("CLIMATE_CUSTOM_FAN_MODE_BASE", layout.CLIMATE_CUSTOM_FAN_MODE_BASE),
        ("CLIMATE_CUSTOM_PRESET_BASE", layout.CLIMATE_CUSTOM_PRESET_BASE),
        *((f"CLIMATE_CAP_{cap.upper()}", bit) for cap, bit in layout.CAPS_NAMES[layout.DOMAIN_CLIMATE].items()),
    ):
        assert f"{name} = {value};" in header or f"{name} = {value}," in header, name
