#!/usr/bin/python3

import argparse
import sys

from tpmspy.trace import tpmspy_trace_open


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Check differences in PCR_Extend hashes over TPM captures")
    parser.add_argument("file", help="Input JSON file")
    return parser


def die(s: str) -> None:
    print(s, file=sys.stderr)
    sys.exit(1)


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    if args.file is None:
        die("Input file is required")

    trace = tpmspy_trace_open(args.file)

    pcrs: list[int] = []
    for event in trace.extends:
        while event[0] >= len(pcrs):
            pcrs.append(0)

        pcrs[event[0]] += 1

    for ix, count in enumerate(pcrs):
        print(f"pcr {ix:02}: {count:02}   \x1b[96m{"━" * count}\x1b[0m")

    print(f"total:  {sum(pcrs):02}")


if __name__ == "__main__":
    main()
