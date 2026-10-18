"""Direct regression for FHS compiler wrapper installation and dispatch."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CompilerTests(unittest.TestCase):
    def test_build_hook_and_compiler_dispatch(self):
        with tempfile.TemporaryDirectory(prefix="nix-compilers-") as tmp:
            root = Path(tmp)
            out = root / "out"
            cwd = root / "build"
            llvm = root / "llvm"
            for directory in (out / "usr/bin", out / "usr/lib", cwd, llvm / "bin"):
                directory.mkdir(parents=True)
            for name in ("clang", "clang++"):
                target = llvm / "bin" / name
                target.write_text("#!/bin/sh\nprintf '%s\\n' \"$0\" \"$@\"\n")
                target.chmod(0o755)
                (out / "usr/bin" / name).symlink_to(target)
            flake = (ROOT / "flake.nix").read_text()
            hook = flake.split("extraBuildCommands = ''", 1)[1].split("\n        '';", 1)[0]
            for variable in ("llvmPackages.clang", "llvmPackages.clang-unwrapped"):
                hook = hook.replace("${" + variable + "}", str(llvm))
            hook = hook.replace("${./scripts/nix-compiler.sh}", str(ROOT / "scripts/nix-compiler.sh"))
            hook = hook.replace("''${", "${")
            result = subprocess.run(["bash", "-eu", "-c", hook], cwd=cwd,
                                    env={**os.environ, "out": str(out)},
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(cwd.iterdir()), [], "hook must operate on $out, not cwd")
            for name, args, expected in (
                ("clang", ["-c", "source with spaces.c"],
                 ["-idirafter", "/usr/include", "-c", "source with spaces.c"]),
                ("clang++", ["-target", "x86_64-apple-darwin", "source.cc"],
                 ["-target", "x86_64-apple-darwin", "source.cc"]),
            ):
                wrapper = out / "usr/bin" / name
                self.assertFalse(wrapper.is_symlink() and wrapper.resolve().is_relative_to(llvm))
                script = wrapper.read_text().replace("/usr/bin/", str(out / "usr/bin") + "/")
                wrapper.write_text(script)
                result = subprocess.run([str(wrapper), *args], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), [str(llvm / "bin" / name), *expected])


if __name__ == "__main__":
    unittest.main()
