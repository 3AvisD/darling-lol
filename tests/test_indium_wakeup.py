"""Compile the actual event-loop methods against a deterministic Vulkan wait fixture."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import test_runtime_compatibility as sources

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
class WakeupChecks(unittest.TestCase):
    patched = sources.CompatibilityChecks.patched

    def test_registration_interleavings_opt_out_shutdown_and_two_devices(self):
        source = self.patched('metal--deps--indium', 'src/indium/device.cpp')
        methods = '\n'.join(sources.function(source, 'Indium::PrivateDevice::' + name)
                            for name in ('pollEvents', 'waitForSemaphore', 'wakeupEventLoop'))
        fixture = (ROOT / 'tests/fixtures/indium-wakeup.cpp').read_text()
        self.run_cpp(fixture.replace('/* ACTUAL_PATCHED_METHODS */', methods))

    def test_live_batch_controls_respect_timer_resolution_and_bounds(self):
        source = self.patched('metal--deps--indium', 'src/indium/command-buffer.cpp')
        tuning = source[source.index('long readTuning('):source.index('void startSubmitBatchTimer(')]
        fixture = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
static const char* fixturePath;
static FILE* test_fopen(const char*, const char* mode) { return ::fopen(fixturePath,mode); }
#define fopen test_fopen
''' + tuning + r'''
#undef fopen
static void write(const char* text) {
    FILE* f=fopen(fixturePath,"w"); assert(f); fputs(text,f); fclose(f);
}
int main(int argc,char** argv) {
    assert(argc==2); fixturePath=argv[1];
    write("100");
    auto& initial=submitBatchTuning();
    assert(initial.windowMicroseconds==500 && initial.maxItems==100);
    write("200000");
    for(int i=0;i<256;++i) submitBatchTuning();
    assert(initial.windowMicroseconds==100000 && initial.maxItems==256);
    write("invalid");
    for(int i=0;i<256;++i) submitBatchTuning();
    assert(initial.windowMicroseconds==500 && initial.maxItems==16);
}
'''
        self.run_cpp(fixture, config=True)

    def run_cpp(self, text, config=False):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'check.cpp').write_text(text)
            compile = subprocess.run(['c++', '-std=c++17', '-pthread', str(path/'check.cpp'),
                                      '-o', str(path/'check')], capture_output=True, text=True)
            self.assertEqual(compile.returncode, 0, compile.stderr)
            result = subprocess.run([str(path/'check'), *([str(path/'tuning')] if config else [])],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
