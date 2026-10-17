# PR #41 compatibility adaptation

[Original PR](https://github.com/lucannez64/darling-lol/pull/41)

This adaptation keeps the compatibility corrections that can be implemented and
checked independently of a fabricated machine or security identity. It starts
from main after PRs #43 and #42. It does not claim to resolve account bans.

## Included

- Foundation returns the installed system's full version/build description,
  falling back to ProductVersion and then Unknown. Numeric version comparisons
  read ProductVersion directly, independently of the localized or overridden
  description. No macOS version or build number is invented.
- physicalMemory queries 64-bit hw.memsize, then retries hw.physmem with its
  correct 32-bit buffer width. The existing failure fallback is preserved.
- Unknown ptrace requests return EINVAL instead of an uninitialized value.
  Existing handled requests retain their behavior.
- Unsupported page-accounting and task-policy requests return
  KERN_NOT_SUPPORTED, initializing output counts where appropriate. Unsupported
  VM-region and thread-info flavors return KERN_INVALID_ARGUMENT. These calls
  no longer terminate darlingserver through unsafe stubs.
- The kernel-debugger capability query returns false and initializes its output.
- The 32-bit debug-state conversion passes the converted structure instead of
  the address of its input pointer. Hardware debug-state support remains
  unsupported.

The deferred compatibility groups are implemented in the [following stack layer](pr41-deferred-implementation.md).

## Work left outside this adaptation

The original PR's rendered source changes were inspected against its merge base,
so patch regeneration noise was excluded from this review.

- Its darling.patch deletes the existing vDSP arith.c and fft.c implementations.
  Those working routines must be preserved.
- GPU reporting is already implemented using actual Vulkan device properties.
  A fixed Radeon Pro name and 4 GB budget would replace that behavior.
- The per-host platform ID fix is already merged. A separate configurable
  identity feature would need an explicit schema, stable defaults, import
  validation, and migration tests before replacing it. Automatic Apple serial,
  NVRAM, OS-version, and model fabrication is excluded.
- CPUID trapping changes the SIGSEGV path and advertises a specific Intel CPU
  on every host. It needs a separate design around actual host capabilities,
  instruction safety, signal chaining, and performance; it is not necessary
  for the compatibility corrections above.
- csops, AMFI, SIP, and debugger APIs must reflect implemented checks and state.
  Fixed signing identities, fabricated trust-cache membership, and successful
  setters that do not apply their settings cannot serve as those implementations.
- IOKit registry lookup/property routines should be extracted with bounds,
  lifetime, traversal, and serialization tests, independent of synthetic NVRAM.
- Launcher environment and descriptor changes need a defined ownership boundary.
  Blanket removal of driver variables or closing descriptors 3 through 256 can
  remove intentional configuration and live descriptors. Keep #42's path setup.
- Log verbosity, descriptor leaks in shellspawn, CPU-frequency units, and other
  independent correctness fixes can be separate changes with focused checks.

The separate [CPUID and security validation](pr41-cpuid-security-validation.md)
records reproduced CPUID defects, the hardware limit on performance testing,
file signature verification and the remaining enforcement requirements.

## Validation

The patch-level C regression tests compile the actual patched function bodies
with inert RPC substitutes and exercise error returns, output initialization,
null output pointers, unknown ptrace requests, and existing ptrace branches:

```sh
python3 -m unittest discover -s tests
python3 scripts/prepare.py --check-only
python3 scripts/validate.py
python3 tools/riot-launcher-offline-check.py
python3 tools/riot-input-offline-check.py
python3 tools/riot-runtime-cleanup-offline-check.py
```

With the adapted patches prepared and Foundation rebuilt, the guest probe checks
full version/build reporting, numeric version independence from an overridden
description, and physicalMemory against 64-bit hw.memsize:

```sh
python3 build/runtime/tools/build-riot-process-info-check.py
darling shell /Volumes/SystemRoot/ABSOLUTE_PROJECT_PATH/build/riot-process-info-check
```

The probe must load the rebuilt Foundation using the configured runtime or an
isolated DYLD_FRAMEWORK_PATH. The adaptation was checked with an isolated
Foundation build; the installed runtime and running server were not replaced.
