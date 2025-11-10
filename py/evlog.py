#!/bin/env python3

import argparse
import difflib
import json
import sys
import yaml

from tpmspy.trace import trace_from_packets
from tpmspy.trace import trace_from_event_log

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


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare TPMSpy Capture with TPM Event Log")
    parser.add_argument("tpmspy", help="TPMSpy JSON capture")
    parser.add_argument("tpmev", help="TPM Event Log")
    parser.add_argument("-H", "--hash", default="SHA1", help="Algorithm to consider")
    return parser


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    tpmspy = trace_from_packets(args.tpmspy, read_json(args.tpmspy)["packets"], args.hash)
    tpmlog = trace_from_event_log(args.tpmev, read_yaml(args.tpmev), args.hash)

    matcher = difflib.SequenceMatcher(None, tpmspy.extend, tpmlog.extend)

    print(f"┌── TPMSpy Capture: {args.tpmspy}")
    print(f"│┌─ TPM Event Log:  {args.tpmev}")
    print("││  PCR  Digest")

    mismatch = 0
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        match tag:
            case "equal":
                for i in range(i1, i2):
                    print(f"\x1b[32m••\x1b[0m  {tpmspy.extend[i][0]:3}  {tpmspy.extend[i][1]}")

            case "insert":
                for j in range(j1, j2):
                    print(f"\x1b[91m •\x1b[0m  \x1b[91m{tpmlog.extend[j][0]:3}  {tpmlog.extend[j][1]}\x1b[0m")
                mismatch += 1

            case "delete":
                for i in range(i1, i2):
                    print(f"\x1b[91m• \x1b[0m  \x1b[91m{tpmspy.extend[i][0]:3}  {tpmspy.extend[i][1]}\x1b[0m")
                mismatch += 1

            case "replace":
                for i, j in zip(range(i1, i2), range(j1, j2)):
                    print(f"\x1b[93m⋱ \x1b[0m  \x1b[93m{tpmspy.extend[i][0]:3}  {tpmspy.extend[i][1]}\x1b[0m")
                    print(f"\x1b[93m ⋱\x1b[0m  \x1b[93m{tpmlog.extend[j][0]:3}  {tpmlog.extend[j][1]}\x1b[0m")
                mismatch += 1

            case _:
                raise Exception(f"Unhandled diff tag {tag}")

    sys.exit(0 if mismatch == 0 else 1)


if __name__ == "__main__":
    main()
