#!/bin/env python3

import argparse
import json
import sys

from tpmspy.trace import InvalidTraceException
from tpmspy.trace import Trace
from tpmspy.trace import trace_from_packets
from tpmspy.trace import TraceList

from typing import Any


def trace_create(file_name: str, packets: Any, alg: str) -> Trace:
    trace: None | Trace = None

    try:
        trace = trace_from_packets(file_name, packets, alg)
    except InvalidTraceException as ex:
        raise Exception(f"{file_name} contains invalid trace") from ex

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
        print(f"Count:  {trace_entry.count}")

        for i, event in enumerate(trace_entry.trace.extend):
            print(f"Event {i:>3}: Extend {event[0]:>2}: {event[1]}")


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Check differences in PCR_Extend hashes over TPM captures")
    parser.add_argument("files", nargs="+", help="Input JSON files")
    parser.add_argument("-H", "--hash", default="SHA1", help="Algorithm to consider")
    parser.add_argument("-v", "--verbose", action="store_true", default=False, help="Show traces even when there is only one")
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
