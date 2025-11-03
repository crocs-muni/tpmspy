#!/bin/env python3

import argparse
import hashlib
import json
import struct
import sys

from base64 import b64encode
from dataclasses import dataclass
from typing import Any
from typing import TypeAlias

Packet: TypeAlias = Any


@dataclass
class Trace:
    extend: list[tuple[int, str]]
    digest: str | None

    def __init__(self) -> None:
        self.extend = []
        self.digest = None

    def add(self, pcr: int, digest: str) -> None:
        self.extend.append((pcr, digest))

    def get_digest(self) -> str:
        if self.digest is not None:
            return self.digest

        m = hashlib.sha1()

        for entry in self.extend:
            m.update(struct.pack("!I", entry[0]))
            m.update(entry[1].encode('utf-8'))

        self.digest = b64encode(m.digest()).decode('utf-8')
        return self.digest


@dataclass
class TraceList:
    @dataclass
    class TraceListEntry:
        trace: Trace
        count: int

        def __init__(self, trace: Trace):
            self.trace = trace
            self.count = 0

        def inc(self) -> None:
            self.count += 1

    traces: dict[str, TraceListEntry]

    def __init__(self) -> None:
        self.traces = {}

    def add_trace(self, trace: Trace) -> None:
        digest = trace.get_digest()

        if digest not in self.traces:
            self.traces[digest] = TraceList.TraceListEntry(trace)

        self.traces[digest].inc()


def _trace_add_packet(file_name: str, trace: Trace, extend: Any, alg: str) -> None:
    pcr: int = extend['pcr-index']

    if pcr is None:
        print(f"{file_name}: PCR_Extend with no pcr-index attribute", file=sys.stderr)
        return

    for entry in extend['digests']:
        if entry['alg']['name'] == alg:
            trace.add(pcr, entry['digest'])
            return

    print(f"{file_name}: PCR_Extend with no {alg} digest", file=sys.stderr)


def trace_create(file_name: str, packets: Any, alg: str) -> Trace:
    trace = Trace()

    for packet in packets:
        if packet['channel'] == "data" \
                and 'SOCKET_LINK_QEMU' in packet['meta']['src']['type'] \
                and packet['req'].get('name', '') == 'CMD_PCR_Extend':
            _trace_add_packet(file_name, trace, packet['attr'], alg)

    if len(trace.extend) == 0:
        print(f"{file_name}: No packets matched", file=sys.stderr)

    return trace


def traces_print(trace_list: TraceList, files: list[str]) -> None:
    print(f"Files:  {len(files):>5}")
    print(f"Traces: {len(trace_list.traces):>5}")
    print()

    for i, digest in enumerate(sorted(trace_list.traces, key=lambda item: item[1])):
        trace_entry = trace_list.traces[digest]
        print(f"===== Trace {i} =====")
        print(f"Digest: {digest}")

        for i, event in enumerate(trace_entry.trace.extend):
            print(f"Event {i:>3}: Extend {event[0]:>2}: {event[1]}")


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Check differences in PCR_Extend hashes over TPM captures")
    parser.add_argument("files", nargs="+", help="Input JSON files")
    parser.add_argument("-H", "--hash", default="SHA1", help="Algorithm to consider")
    parser.add_argument("-v", "--verbose", type=bool, default=False, help="Show traces even when there is only one")
    return parser


def die(s: str) -> None:
    print(s, file=sys.stderr)
    sys.exit(1)


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    if len(args.files) < 1:
        die("Too few input files")

    trace_list = TraceList()

    for file_name in args.files:
        with open(file_name, 'r') as file:
            data = json.load(file)

        if not (packets := data.get("packets", [])):
            print(f"{file_name}: No packets found")
            continue

        trace = trace_create(file_name, packets, args.hash)
        if len(trace.extend) > 0:
            trace_list.add_trace(trace)

    total_traces = sum(map(lambda i: i.count, trace_list.traces.values()))
    if len(trace_list.traces) == 1 and total_traces == len(args.files) \
            and not args.verbose:
        print("OK: All traces match")
    else:
        traces_print(trace_list, args.files)


if __name__ == "__main__":
    main()
