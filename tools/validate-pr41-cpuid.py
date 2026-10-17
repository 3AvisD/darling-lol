#!/usr/bin/env python3
"""Audit the exact #41 CPUID candidate in isolated Linux subprocesses."""
import argparse
import json
import hashlib
import os
import signal
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'e948b4cff0424e0937396fca6602a820b7b42811'
PATCH_SHA256 = 'f0ac7e85d0a144823e556a4b90840daa2414a05259ac5580f167e8c17f78844a'


def candidate(patch):
    section = patch.split('diff --git a/src/startup/mldr/mldr.c b/src/startup/mldr/mldr.c\n', 1)[1]
    added = '\n'.join(line[1:] for line in section.split('\ndiff --git ', 1)[0].splitlines()
                      if line.startswith('+') and not line.startswith('+++'))
    start = added.index('#ifdef __x86_64__')
    end = added.index('\n#endif', added.index('static void mldr_setup_cpuid_virtualization'))
    return added[start:end + len('\n#endif')] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch', type=Path, help='Original PR darling.patch; otherwise read the pinned git object')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/pr41-cpuid-validation.json')
    args = parser.parse_args()
    patch = args.patch.read_text() if args.patch else subprocess.check_output(
        ['git', 'show', REVISION + ':patches/darling.patch'], cwd=ROOT, text=True)
    if hashlib.sha256(patch.encode()).hexdigest() != PATCH_SHA256:
        parser.error('This extractor supports only the exact reviewed original #41 patch')
    harness = (ROOT / 'tools/validation/pr41-cpuid-harness.c').read_text()
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'check.c'
        binary = Path(directory) / 'check'
        source.write_text(harness.replace('/* ORIGINAL_CANDIDATE */', candidate(patch)))
        compiler = subprocess.check_output(['cc', '--version'], text=True).splitlines()[0]
        subprocess.run(['cc', '-O2', '-std=gnu11', '-pthread', str(source), '-o', str(binary)], check=True)
        binary_sha256 = hashlib.sha256(binary.read_bytes()).hexdigest()
        process = subprocess.Popen([str(binary)], text=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, start_new_session=True)
        try:
            stdout, stderr = process.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
            raise RuntimeError('CPUID harness timed out; entire probe process group terminated')
        if process.returncode:
            raise RuntimeError('CPUID harness failed: ' + stderr)
    observations = [json.loads(line) for line in stdout.splitlines()]
    cpu = {}
    for line in Path('/proc/cpuinfo').read_text().split('\n\n', 1)[0].splitlines():
        key, sep, value = line.partition(':')
        if sep and key.strip() in ('vendor_id', 'model name', 'cpu family', 'model', 'stepping'):
            cpu[key.strip()] = value.strip()
    report = {'revision': REVISION, 'cpu': cpu, 'compiler': compiler,
              'harness_sha256': hashlib.sha256(harness.encode()).hexdigest(),
              'binary_sha256': binary_sha256, 'patch_sha256': hashlib.sha256(patch.encode()).hexdigest(),
              'patch_source': str(args.patch) if args.patch else 'pinned git object',
              'feature_coverage': 'named features plus raw masks for seven feature registers; host-specific, not instruction emulation', 'host': platform.platform(), 'observations': observations,
              'scope': 'Linux isolated harness of exact original functions; not a guest integration or game benchmark',
              'trap_validation': ('unavailable on this host' if any(item.get('supported') is False for item in observations)
                                  else 'failed' if any(item.get('pass') is False for item in observations if item.get('check') in ('hardware_faulting', 'faulting_child_completed'))
                                  else 'isolated CPUID trap observed; guest integration outstanding'),
              'accepted': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 2 if any(item.get('pass') is False for item in observations) else 0


if __name__ == '__main__':
    raise SystemExit(main())
