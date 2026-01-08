#!/usr/bin/python3

import argparse
import difflib
import sys

from tpmspy.trace import TraceSlice
from tpmspy.trace import tpmspy_trace_open
from tpmspy.trace import TraceList


def traces_print(trace_list: TraceList, files: list[str]) -> None:
    print(f"Files:  {len(files):>5}")
    print(f"Traces: {len(trace_list.traces):>5}")
    print()

    for i, digest in enumerate(sorted(trace_list.traces, key=lambda item: item[1])):
        trace_entry = trace_list.traces[digest]
        print(f"===== Trace {i} =====")
        print(f"Digest: {digest}")
        print(f"Count:  {trace_entry.count}")

        for i, event in enumerate(trace_entry.trace.extends):
            print(f"Event {i:>3}: Extend {event[0]:>2}")

            for alg in sorted(event[1]):
                print(f"{"":4}{alg:6} {event[1][alg]}")


def _trace_compare_diff(trace_a: TraceSlice, trace_b: TraceSlice) -> None:
    matcher = difflib.SequenceMatcher(None, trace_a, trace_b)

    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        match tag:
            case "equal":
                pass

            case "insert":
                for j in range(j1, j2):
                    print(f"\x1b[92m +\x1b[0m  \x1b[92m{trace_b[j][0]:3}  {trace_b[j][1]}\x1b[0m")

            case "delete":
                for i in range(i1, i2):
                    print(f"\x1b[91m- \x1b[0m  \x1b[91m{trace_a[i][0]:3}  {trace_a[i][1]}\x1b[0m")

            case "replace":
                for i in range(i1, i2):
                    print(f"\x1b[91m- \x1b[0m  \x1b[91m{trace_a[i][0]:3}  {trace_a[i][1]}\x1b[0m")
                for j in range(j1, j2):
                    print(f"\x1b[92m +\x1b[0m  \x1b[92m{trace_a[j][0]:3}  {trace_b[j][1]}\x1b[0m")

            case _:
                raise Exception(f"Unhandled diff tag {tag}")


def traces_compare(trace_list: TraceList, files: list[str]) -> None:
    print(f"Files:  {len(files):>5}")
    print(f"Traces: {len(trace_list.traces):>5}")
    print()

    if len(trace_list.traces) == 1:
        print("No meaningful diff can be created for a single trace", file=sys.stderr)
        return

    sorted_digests = list(sorted(trace_list.traces, key=lambda item: item[1]))
    trace_0 = trace_list.traces[sorted_digests[0]]
    algs_0 = trace_0.trace.all_algorithms()

    for i in range(1, len(sorted_digests)):
        print(f"===== 0 vs {i} =====")
        trace_i = trace_list.traces[sorted_digests[i]]
        algs_i = trace_i.trace.all_algorithms()

        for alg in sorted(algs_0 | algs_i):
            if alg not in (algs_0 & algs_i):
                print(f"{alg}: Not present in both traces", file=sys.stderr)
                continue

            print(f"----- {alg} -----")
            _trace_compare_diff(trace_0.trace.select_digests(alg), trace_i.trace.select_digests(alg))


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Check differences in PCR_Extend hashes over TPM captures")
    parser.add_argument("files", nargs="+", help="Input JSON files")
    parser.add_argument("-d", "--diff", action="store_true", default=False, help="Show differences between the first trace and the rest")
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
        trace = tpmspy_trace_open(file_name)

        if len(trace.extends) > 0:
            trace_list.add_trace(trace)

    total_traces = sum(map(lambda i: i.count, trace_list.traces.values()))
    if len(trace_list.traces) == 1 and total_traces == len(args.files) \
            and not args.verbose:
        print("OK: All traces match")
    elif args.diff:
        traces_compare(trace_list, args.files)
    else:
        traces_print(trace_list, args.files)


if __name__ == "__main__":
    main()
