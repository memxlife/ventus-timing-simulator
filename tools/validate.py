#!/usr/bin/env python3
"""Compare fresh C++ executions with six saved RTL observations."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
KINDS = {'collector_accept', 'collector_issue', 'collector_operand_return', 'writeback_x', 'writeback_v'}

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def load(path):
    return json.loads((ROOT / path).read_text())

def invoke(executable, *args):
    return subprocess.run([str(ROOT / 'build' / executable), *map(str, args)], check=True, capture_output=True, text=True).stdout

def parse(output):
    execution, writes, host = [], [], []
    for row in output.splitlines():
        fields = row.split('\t')
        tick, kind = int(fields[0]), fields[1]
        if kind in KINDS:
            execution.append([tick, kind, int(fields[2]), int(fields[3])])
        elif kind == 'physical_write':
            mask = sum(int(word) << (32 * i) for i, word in enumerate(fields[3:]))
            writes.append([tick * 5 + 5, int(fields[2]), mask])
        elif kind == 'host_return':
            host.append(tick * 10 + 5)
    return sorted(execution), sorted(writes), host

def compare(output, expected, label):
    execution, writes, host = parse(output)
    require(execution == expected['execution'], label + ': execution events differ')
    require(writes == expected['physical_writes'] and len(writes) == 16, label + ': physical writes differ')
    require(host == expected['host_return'], label + ': host return differs')
    require(max(host + [row[0] for row in writes]) == expected['completion'], label + ': completion differs')

manifest = load('MANIFEST.json')
for filename, expected_hash in manifest.items():
    require(hashlib.sha256((ROOT / filename).read_bytes()).hexdigest() == expected_hash, filename + ': hash differs')

provenance = load('PROVENANCE.json')
original = load('evidence/original-execution-validation.json')
benchmark = load('evidence/historical-benchmark.json')
require(len(original) == 12 and len(benchmark['results']) == 6, 'Expected six programs and two clock modes')
for name, source in provenance['programs'].items():
    program = ROOT / 'fixtures' / name / 'program.txt'
    expected = load(f'fixtures/{name}/expected.json')
    require(hashlib.sha256(program.read_bytes()).hexdigest() == source['original_program_sha256'], name + ': input changed')
    for mode in ('cycle', 'skip'):
        compare(invoke('ventus-machine-timing', program, *(['skip'] if mode == 'skip' else [])), expected, name + '/' + mode)
        archived = next(row for row in original if row['run'] == name and row['mode'] == mode)
        require(archived['execution_match'] and archived['write_match'] and archived['host_match'], name + ': archived comparison failed')
        require(archived['input_sha256'] == source['original_program_sha256'] and archived['completion'] == expected['completion'], name + ': archived input/endpoint differs')
    compare(invoke('ventus-benchmark', program, 1, 'verify'), expected, name + '/benchmark')
    current = json.loads(invoke('ventus-benchmark', program))
    require(current['endpoint_time'] == expected['completion'] and current['writes'] == 16, name + ': benchmark completion differs')
    require(current['last_rising_time'] <= current['endpoint_time'] < current['last_rising_time'] + 10, name + ': stopping boundary differs')
    require(current['payload_evaluations'] == 0, name + ': payload work reported')
    numerical = load(f'evidence/{name}-numerical-output.json')
    require(numerical['observed_words'] == numerical['expected_words'] and len(numerical['expected_words']) == 128, name + ': historical numerical outputs differ')
    receipt = next(row for row in benchmark['results'] if row['run'] == name)
    require(receipt['endpoint_time'] == expected['completion'], name + ': historical endpoint differs')
    require(len(receipt['samples']) == benchmark['repetitions'] == 7, name + ': sample count differs')
    for median, sample in [('median_rtl_body_ns', 'rtl_body_ns'), ('median_cpp_body_ns', 'cpp_single_body_ns'), ('median_rtl_process_ns', 'rtl_process_ns'), ('median_cpp_process_ns', 'cpp_process_ns')]:
        require(statistics.median(row[sample] for row in receipt['samples']) == receipt[median], name + ': historical median differs')
    for ratio, rtl, cpp in [('body_speed_ratio', 'median_rtl_body_ns', 'median_cpp_body_ns'), ('process_speed_ratio', 'median_rtl_process_ns', 'median_cpp_process_ns')]:
        require(abs(receipt[ratio] - receipt[rtl] / receipt[cpp]) < 1e-9, name + ': historical ratio differs')
    print(name + ': cycle, skip, benchmark events and completion match saved RTL')
print('Six programs validated; packaged hashes and historical measurements agree. Fresh RTL was not run.')
