#!/usr/bin/python3

import argparse
import difflib
import json
import sys
import yaml

from tpmspy.trace import event_log_open
from tpmspy.trace import tpmspy_trace_open
from tpmspy.trace import Trace
from tpmspy.trace import TraceSlice

from typing import Any


def read_json(file_name: str) -> dict[str, Any]:
    with open(file_name, "r") as file:
        data = json.load(file)
        assert isinstance(data, dict)
        return data


def read_yaml(file_name: str) -> dict[str, Any]:
    with open(file_name, "r") as file:
        data = yaml.load(file, Loader=yaml.Loader)
        assert isinstance(data, dict)
        return data


def compare_traces(args: argparse.Namespace, tpmspy: TraceSlice, tpmlog: TraceSlice) -> int:
    matcher = difflib.SequenceMatcher(None, tpmspy, tpmlog)

    opcodes = matcher.get_opcodes()
    if len(opcodes) == 1 and opcodes[0][0] == "equal" and not args.verbose:
        print("No differences")
        return 0

    mismatch = 0

    print(f"┌── TPMSpy Capture: {args.tpmspy}")
    print(f"│┌─ TPM Event Log:  {args.tpmev}")
    print("││  PCR  Digest")

    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        match tag:
            case "equal":
                for i in range(i1, i2):
                    print(f"\x1b[36m  \x1b[0m  {tpmspy[i][0]:3}  {tpmspy[i][1]}")

            case "delete":
                for i in range(i1, i2):
                    print(f"\x1b[91m- \x1b[0m  \x1b[91m{tpmspy[i][0]:3}  {tpmspy[i][1]}\x1b[0m")
                mismatch += 1

            case "insert":
                for j in range(j1, j2):
                    print(f"\x1b[92m +\x1b[0m  \x1b[92m{tpmlog[j][0]:3}  {tpmlog[j][1]}\x1b[0m")
                mismatch += 1

            case "replace":
                for i in range(i1, i2):
                    print(f"\x1b[91m- \x1b[0m  \x1b[91m{tpmspy[i][0]:3}  {tpmspy[i][1]}\x1b[0m")
                for j in range(j1, j2):
                    print(f"\x1b[92m +\x1b[0m  \x1b[92m{tpmlog[j][0]:3}  {tpmlog[j][1]}\x1b[0m")

                mismatch += 1

            case _:
                raise Exception(f"Unhandled diff tag {tag}")

    return mismatch


def query_algorithms(tpmspy: Trace, tpmev: Trace) -> None:
    for name, trace in [("TPM Spy Log", tpmspy), ("TPM2 Event Log", tpmev)]:
        print(f"{name:16} all:    {trace.all_algorithms()}")
        print(f"{"":16} common: {trace.common_algorithms()}")


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare TPMSpy Capture with TPM Event Log")
    parser.add_argument("tpmspy", help="TPMSpy JSON capture")
    parser.add_argument("tpmev", help="TPM Event Log")
    parser.add_argument("--algorithms", "-A", action="store_true", help="Instead of comparing hashes, list all algorithms")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show full differences with equal sections")
    return parser


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    tpmspy = tpmspy_trace_open(args.tpmspy)
    tpmlog = event_log_open(args.tpmev)

    if args.algorithms:
        query_algorithms(tpmspy, tpmlog)
        sys.exit(0)

    algs_tpmspy = tpmspy.all_algorithms()
    algs_tpmlog = tpmlog.all_algorithms()

    mismatch = 0

    for alg in sorted(algs_tpmspy | algs_tpmlog):
        if alg not in algs_tpmspy & algs_tpmlog:
            print(f"{alg}: Algorithm skipped as it is not present in both traces")
            continue

        print(f"----- {alg} -----")
        mismatch += compare_traces(args, tpmspy.select_digests(alg), tpmlog.select_digests(alg))

    sys.exit(0 if mismatch == 0 else 1)


if __name__ == "__main__":
    main()
