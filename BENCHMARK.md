# Historical matched runtime benchmark

How much faster was the timing-only model than the pinned RTL build for these six programs? The saved comparison measures both simulators through the same endpoint: the later of host return and the final physical output write. Each table value is the median of seven fresh-process samples after one unmeasured warm-up. Smaller runtime is better. Speedup is RTL time divided by C++ time.

| Program | Endpoint | RTL process, ms | C++ process, ms | Process speedup | RTL body, ms | C++ body, ms | Body speedup |
|---|---:|---:|---:|---:|---:|---:|---:|
| original | 35070 | 439.301 | 3.008 | 146.0× | 429.560 | 0.1785 | 2406× |
| delay1 | 4190 | 63.096 | 2.941 | 21.5× | 53.662 | 0.1678 | 320× |
| delay16 | 4225 | 64.650 | 3.020 | 21.4× | 55.247 | 0.1810 | 305× |
| full-software | 4430 | 66.147 | 3.145 | 21.0× | 56.528 | 0.1681 | 336× |
| timing-bound | 35150 | 439.841 | 3.049 | 144.3× | 430.364 | 0.1836 | 2344× |
| store-spacing | 4150 | 62.465 | 3.060 | 20.4× | 53.122 | 0.1635 | 325× |

Process time includes startup, parsing, initialization, simulation, result formatting and cleanup. Body time excludes parsing and output serialization. The C++ body constructs fresh model state and retains in-memory events. Headline body measurements run one program per fresh process on both sides; separately recorded 100-run C++ batches do not supply these ratios. Compilation is excluded.

Before measurement, the exact C++ benchmark executable was compared with the selected RTL execution events, all 16 physical writes and host return. Independently, the RTL benchmark executable produced all 128 expected numerical output words at the endpoint. Those numerical checks were outside measured runs. Both checks are archived here; the standalone tests compare newly built C++ executables against the saved RTL observations.

The RTL benchmark linked the unchanged emitted RTL archive. Its owned host entry point disabled waveform capture and logs, removed the usual fixed drain interval and stopped at the endpoint. Two Verilator file-print helper bodies were disabled to remove generated diagnostics. The recorded RTL objects used `-O1`; C++ used `-O2`. These numbers describe those local builds, not the best performance attainable by rebuilding RTL with different settings.

The original and timing-bound programs skip 3072 idle memory steps during cache invalidation. The body speedups combine a smaller timing representation, omitted numerical work and cycle skipping. They do not measure any one mechanism in isolation. The completion check accounts for a final falling-edge write without processing the following rising edge.

## Evidence and reproduction boundary

- [Raw samples and original executable/source hashes](evidence/historical-benchmark.json)
- [Original RTL build receipt](evidence/historical-rtl-build.json)
- [Original six-program validation in both clock modes](evidence/original-execution-validation.json)
- [Fixture expectations extracted from recorded RTL](fixtures)
- [Standalone source and fixture provenance](PROVENANCE.json)

`make test` reproduces timing agreement against the packaged observations, checks the historical medians and ratios, and confirms the endpoint boundary for freshly built C++ executables. The numerical-output JSON files preserve the original expected and observed RTL words. They are historical evidence, not a fresh numerical execution.

Re-running the complete historical benchmark requires the pinned generated RTL, compiled objects, Verilator runtime and owned host instrumentation. Those dependencies and binaries are not packaged. The standalone source has include-path and attribution changes, so its binary hashes are expected to differ from the original receipt. This publication does not claim a newly measured RTL speedup or coverage beyond the six fixed programs.
