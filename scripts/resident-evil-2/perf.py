"""Measure the upper-bound native RE2 video workload on the mGBA core."""
import argparse
import ctypes
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
from contextlib import contextmanager
from uuid import uuid4


ROOT = Path(__file__).resolve().parents[2]

@contextmanager
def staging_directory(build):
    work = build / ("video-" + uuid4().hex)
    work.mkdir()
    try:
        yield work
    finally:
        shutil.rmtree(work)


def diagnostic_rom(fixture, driver="video.c"):
    source = ROOT / "gba/resident-evil-2"
    build = ROOT / "build/resident-evil-2"
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / "build/zig-cache"))
    with staging_directory(build) as work:
        def run(*args):
            subprocess.run(args, cwd=work, env=env, check=True)

        def compile_source(path, target):
            obj = path.stem + ".o"
            run("zig", "cc", "-target", target + "-freestanding-eabi", "-mcpu=arm7tdmi",
                "-Oz", "-ffreestanding", "-fno-unwind-tables", "-Wall", "-Werror",
                "-I", source, "-c", path, "-o", obj)
            return obj

        objects = [compile_source(source / "start.S", "arm"),
                   compile_source(source / driver, "thumb"),
                   compile_source(fixture, "thumb")]
        run("zig", "ld.lld", "--oformat=binary", "-T", source / "rom.ld", *objects,
            "-o", "perf.gba")
        fix = os.environ.get("GBAFIX") or shutil.which("gbafix") or (
            Path(os.environ["LOCALAPPDATA"]) / "Programs/gba-tools/gbafix.exe")
        run(fix, "perf.gba", "-tRE2-VIDEO", "-cB2VE", "-m00")
        return (Path(work) / "perf.gba").read_bytes()

def frame_runner():
    # Reuse the project's existing mGBA frame runner with an empty input tape.
    spec = importlib.util.spec_from_file_location(
        "mgba_preview", ROOT / "scripts/duke3d/test_preview.py")
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    return runner


def benchmark(core):
    rom = diagnostic_rom(Path(__file__).with_suffix(".c").resolve())
    runner = frame_runner()
    samples = []

    def observe(frame, memory):
        if 0x03000000 in memory:
            values = struct.unpack("<4I", ctypes.string_at(memory[0x03000000] + 0x7000, 16))
            if 2 <= values[0] <= 256:
                samples.append(values)

    pixels = runner.render(core, rom, final_frame=180, video_probe=observe)
    assert len(samples) >= 120, len(samples)
    for prev, curr in zip(samples, samples[1:]):
        assert curr[0] == prev[0] + 1, (prev, curr)
    assert all(4300 < ticks < 4480 and 160 <= line < 228 and not error
               for _, ticks, line, error in samples), samples
    assert pixels[0] == pixels[239] == (0, 0, 0)
    assert pixels[30] == (0, 31, 0), pixels[30]
    assert pixels[100 * 240 + 210] in ((31, 0, 0), (0, 0, 31))
    print(f"{len(samples)} successive presentations; "
          f"{min(s[1] for s in samples)}..{max(s[1] for s in samples)} timer ticks "
          f"(64 cycles/tick); latest commit scanline {max(s[2] for s in samples)}; "
          f"diagnostic ROM {len(rom)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, default=os.environ.get("MGBA_LIBRETRO"))
    options = parser.parse_args()
    core = options.core or next((Path(os.environ["LOCALAPPDATA"]) / "Programs/mGBA")
                                .glob("libretro-*/mgba_libretro.dll"))
    benchmark(core)
