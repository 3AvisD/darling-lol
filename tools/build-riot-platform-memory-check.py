#!/usr/bin/env python3
"""Build and run the host-only checks for the libplatform string primitives.

Two copies of the same libplatform sources are compiled with host clang:

  current - the flags the installed libsystem_platform.dylib was built with:
            memmove.c, bzero.c and memset_pattern.c at -O2 -fno-builtin, every
            other string source at the library default (-O0);
  new     - the flags tools/build-riot-platform-strings.py uses for the
            overlay: -O2 -fno-builtin -fno-strict-aliasing for every string
            source.

The check binary links both copies and compares them against a byte-loop
reference with guard pages on both sides of every buffer, for the generic path
and for the vector and REP paths; the bench binary measures both copies and the
host's glibc. Nothing here touches a Darling process, prefix or guest file.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PLATFORM = ROOT / "build/darling-src/src/external/libplatform"
# configure.py renders the project root; the rendered copy runs from
# build/runtime/tools/, where the shim directory does not exist.
SHIM_TOKEN = "@PROJECT_ROOT@/tools/riot-platform-memory-shim"
SHIM = ROOT / "tools/riot-platform-memory-shim" if SHIM_TOKEN.startswith("@") else Path(SHIM_TOKEN)
SOURCES = ["memmove.c", "bzero.c", "memset_pattern.c", "memcmp.c", "memchr.c",
           "strlen.c", "strcmp.c"]
# The three files the configured build already compiles optimized.
OPTIMIZED_IN_CURRENT = {"memmove.c", "bzero.c", "memset_pattern.c"}
RENAMES = ["_platform_memmove", "_platform_memmove_avx2", "_platform_memset",
           "_platform_memset_avx2", "_platform_bzero", "_platform_memset_pattern4",
           "_platform_memset_pattern8", "_platform_memset_pattern16",
           "_platform_memcmp", "_platform_memcmp_avx2", "_platform_memchr",
           "_platform_memchr_avx2", "_platform_strlen", "_platform_strcmp", "memset"]


def flags(variant, source):
    common = ["-w", "-fno-builtin", "-DLONG_BIT=64", "-include", str(SHIM / "force.h"),
              "-I", str(SHIM), "-I", str(PLATFORM / "private"),
              "-I", str(PLATFORM / "include"), "-I", str(PLATFORM / "internal")]
    common += ["-D%s=%s" % (name, variant + (name if name.startswith("_") else "_" + name))
               for name in RENAMES]
    if variant == "current":
        optimization = ["-O2"] if source in OPTIMIZED_IN_CURRENT else ["-O0"]
    else:
        optimization = ["-O2", "-fno-strict-aliasing"]
    return common + optimization


def objects(folder, variant, extra):
    built = []
    for source in SOURCES:
        obj = folder / ("%s-%s.o" % (variant, source.replace("/", "_")))
        subprocess.run(["clang", *flags(variant, source), *extra,
                        "-c", str(PLATFORM / "src/string/generic" / source), "-o", str(obj)],
                       check=True)
        built.append(str(obj))
    return built


def build(program, source, extra=(), run=()):
    folder = ROOT / "build"
    binary = folder / program
    subprocess.run(["clang", "-O2", "-w", *extra, str(ROOT / "tools" / source),
                    *objects(folder, "current", extra), *objects(folder, "new", extra),
                    "-o", str(binary)], check=True)
    print("Built", binary)
    for arguments in run:
        subprocess.run([str(binary), *arguments], check=True)


def main():
    build("riot-platform-memory-check", "riot-platform-memory-check.c",
          run=[("--no-avx2",), ("--avx2",), ("--avx2-erms",)])
    build("riot-platform-memory-check-asan", "riot-platform-memory-check.c",
          extra=["-fsanitize=address,undefined", "-fno-sanitize-recover=all"],
          run=[("--no-avx2",), ("--avx2-erms",)])
    build("riot-platform-memory-bench", "riot-platform-memory-bench.c", run=[()])


if __name__ == "__main__":
    main()
