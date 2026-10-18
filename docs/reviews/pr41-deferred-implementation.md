# PR #41 deferred compatibility work

This follow-up is stacked on the compatibility extraction in PR #44. It
implements the four groups deferred in that review, with explicit configuration
and observable errors rather than automatic identity changes.

## Shell spawning

Diagnostics are quiet by default; `DARLING_SHELLSPAWN_DEBUG=1` enables the
existing diagnostic output. The environment and arguments are no longer dumped
on normal launches.

Stdio sources are duplicated before any standard descriptor is replaced, so
aliases involving descriptors 0–2 are safe. Stream cleanup closes each owned
descriptor once instead of repeatedly closing stream 0. Both fork branches drop
unneeded descriptors, and error paths clean up the exec-status pipe. The
control socket is kept out of the stdio slots. Terminal signal forwarding uses
the remapped stdin. Unrelated descriptors and the configured shell selection
are preserved.

## CPU frequency

`hw.cpufrequency` is an unsigned 64-bit frequency in Hz, matching XNU's named
API. Linux cpufreq kHz values are checked for invalid input and overflow before
conversion. The exported host sysfs location is tried as well as `/sys`; an
available native CPUID base-frequency leaf is a fallback. No fixed CPU speed is
invented when no source exists.

Numeric `HW_CPU_FREQ` retains the legacy 32-bit representation through a
separate compatibility entry, matching XNU's distinction between the modern
named API and the numeric compatibility API. Consumers needing frequencies
above the 32-bit range should use the named 64-bit API.

## IOKit registry

- Inline and out-of-line path lookup validate termination, path shape, plane
  membership, and length. Paths resolve registered entries with case-sensitive
  names; no synthetic firmware nodes are inserted.
- Path output uses inline storage when possible and a bounded sender buffer for
  long physical-copy replies. Incoming OOL mappings are released on success and
  error paths. The daemon's main queue serializes request processing and reply
  copying, so reply storage remains live through the send.
- Plane membership follows registered ancestry. Deleting a parent or child
  removes both graph links. Plane-name keys own their strings, and cyclic
  registration is refused.
- Raw property access handles strings, data, integer numbers at their reported
  widths, and XNU's `Yes`/`No` representation of booleans. It bounds writes by
  both the requested count and the fixed MIG array. Missing and unsupported
  values return errors; small buffers return IPC errors without writes.

## Platform configuration

[Platform identity configuration](../PLATFORM_IDENTITY.md) documents the schema,
OpenCore/GenSMBIOS/legacy imports, startup snapshot, defaults, and rollback.
Neither installation nor launch generates an override file. Existing host-based
identifiers remain stable unless the user explicitly supplies replacements or
requests synthetic per-prefix provisioning.

## Validation

Portable regression checks:

```sh
python3 -m unittest discover -s tests
python3 scripts/prepare.py --check-only
python3 scripts/validate.py
python3 tools/riot-launcher-offline-check.py
python3 tools/riot-input-offline-check.py
python3 tools/riot-runtime-cleanup-offline-check.py
```

The unit tests compile the actual patched fd helper and frequency code. They
exercise fd aliases, unrelated descriptors, repeated failed remaps, conversion
overflow, buffer widths, schema validation, imports, idempotency, backups,
per-prefix isolation, malformed input, and preservation of existing identities.

With the source patches prepared, build an isolated iokitd candidate and probe:

```sh
python3 build/runtime/tools/build-riot-registry-compatibility-check.py
darling shell /Volumes/SystemRoot/ABSOLUTE_PROJECT_PATH/build/riot-registry-compatibility-check
```

The probe uses the actual daemon objects and MIG handlers, and creates its own
Mach ports. It does not register a daemon or replace the running iokitd. It
checks traversal, deletion, cycles, malformed OOL input, 1,000 direct OOL
lookups, 100 real IPC path round trips, property widths/bounds, and binary
serialization round trips. It also checks daemon-side identity validation.

For a rebuilt kernel containing the frequency patch:

```sh
python3 build/runtime/tools/build-riot-cpu-frequency-check.py
darling shell /Volumes/SystemRoot/ABSOLUTE_PROJECT_PATH/build/riot-cpu-frequency-check
```

Local validation also ran the fd helper inside Darling and used an isolated
kernel override for the frequency probe. The configured production runtime was
not replaced, no real identity was changed, and no match capture is required.


Opt-in provisioning now fills missing synthetic serial/MLB, UUID, system-id,
provisioning UUID alias and ROM fields. It repairs the two known shared defaults
while retaining custom values, synchronizes the canonical and legacy config,
and copies actual runtime SystemVersion metadata. Disposable-prefix tests cover
concurrent writers, idempotency, migration, backups, live-prefix refusal, and
preflight failures. The registry probe validates all ten allowed properties.
See [platform identity usage](../PLATFORM_IDENTITY.md#opt-in-provisioning).
