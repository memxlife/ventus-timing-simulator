# Ventus Timing Simulator

Can a small simulator predict when a Ventus matrix kernel finishes without calculating its matrix values? This C++17 model predicts selected instruction events, host return and physical output writes for six checked machine programs. It models one warp of 32 active lanes multiplying an 8-by-2 matrix by a 2-by-16 matrix, producing 128 single-precision floating-point words.

The model calculates addresses and follows register readiness, operand collection, execution occupancy, cache queues and memory responses. It omits floating-point arithmetic and numerical memory values. Recorded RTL event times are used only by the validation script; they are never simulator inputs. RTL means register-transfer-level hardware simulation.

## Build and validate

A C++17 compiler, Make and Python 3 are sufficient. No additional Python packages, GPU, RTL checkout or Verilator installation are needed.

```sh
make
make test
./build/ventus-machine-timing fixtures/store-spacing/program.txt skip
```

Omit `skip` to visit every clock cycle. `make test` runs all six programs in both modes, compares selected execution events, every physical-write time/address/byte mask and host return against the included RTL observations, and checks completion with the benchmark executable. It also verifies packaged file hashes and historical benchmark arithmetic. This reproduces the timing comparisons against saved observations; it does not run fresh RTL or verify floating-point values through C++.

The six programs are the original, one- and sixteen-NOP termination variants, full-software, timing-bound and store-spacing candidates. A NOP does no useful arithmetic but can change execution timing. Each program has 16 physical output writes. In the store-spacing example, host return occurs at time 3625 and the last output write at 4150, so completion is 4150. Host return alone would give the wrong completion time.

## Input and output

The first input line contains four decimal integers: the first cycle after reset, the earliest host-offer cycle, and two initial instruction-cache FIFO source words. FIFO means first in, first out. The supported reset value is 2. Subsequent lines contain a hexadecimal program counter and a hexadecimal instruction word. The packaged programs use host-offer cycle 3 and initial words `[16, 10]`, observed for the pinned seed-10086 RTL family. Those words are initial state, not universal architecture constants.

The command emits tab-separated events. Execution events and `host_return` use rising-edge cycle indices. Convert their first column `c` to simulated time with `10*c + 5`. A `physical_write` uses half-cycle index `h`; convert it with `5*h + 5`. Its remaining fields contain the address and four 32-bit portions of a 128-bit byte mask, least significant portion first. One full hardware clock cycle equals ten simulated time units. `steps` and `skipped` are counts, not event times.

Completion is the later of host return and the last physical write. A write can occur halfway between rising edges. The stopping condition includes that write without processing the next rising edge. The test requires the last processed rising edge to be at or before completion and the next rising edge to be after completion.

## Checked scope and limits

The model supports a bounded straight-line instruction subset, one fully active warp and the specified cold initial state. The read-to-store transition requires drained read queues, clean retained lines and output stores that miss those lines. Unsupported store hits and overlapping read/store phases are rejected. The run requires exactly 16 physical writes and has a fixed maximum cycle count.

Agreement on six programs does not establish accuracy for arbitrary instruction schedules, register assignments, warm caches, multiple warps or other GPU configurations. Omitted payloads cannot supply data-dependent addresses or branches. A new instruction form or cache interaction needs implementation and a fresh RTL comparison before use.

## Timing measurements

The [historical benchmark](BENCHMARK.md) reports 20–146 times faster complete processes and 305–2406 times faster simulation bodies than pinned local RTL builds. These ranges compare medians of seven samples. They include omitted numerical work and idle-cycle skipping; they do not isolate either contribution. The archived builds and RTL dependencies are not distributed here, so this checkout reproduces saved timing agreement and C++ measurements, rather than the complete historical RTL benchmark.

To measure the current C++ body once in a fresh process:

```sh
./build/ventus-benchmark fixtures/original/program.txt
```

An optional second argument repeats the model within one process; for example, `100` measures a batch. Divide `body_ns` by the repetition count for per-run body time. `parse_ns` measures parsing separately. Neither quantity is complete process time, and neither establishes a new speedup over RTL.

## Provenance and licensing

[NOTICE](NOTICE) records the pinned Ventus and SiFive inclusive-cache sources and their licenses. [PROVENANCE.json](PROVENANCE.json) maps the standalone files to the original timing-model hashes and records the source hashes of each saved RTL trace. The fixtures were emitted by a bounded local instruction encoder, rather than a Ventus compiler. The included numerical-output records preserve the independent historical RTL comparison of all 128 words; the timing model never computes those values.

The standalone project uses Mulan PSL v2, with retained upstream notices and Apache-2.0 terms for the inclusive-cache-derived timing rules. See [LICENSE](LICENSE) and [licenses](licenses). This package contains simulator deliverables only.
