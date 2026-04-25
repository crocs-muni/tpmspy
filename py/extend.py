#!/usr/bin/env python3

import sys
import hashlib

from hashlib import _Hash
from typing import Callable
from typing import cast


def get_hash(name: str) -> Callable[[], _Hash]:
    try:
        return cast(Callable[[], _Hash], getattr(hashlib, name))
    except AttributeError:
        print(f"Unsupported hash: {name}", file=sys.stderr)
        sys.exit(1)


def main() -> None:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <hashname>", file=sys.stderr)
        sys.exit(1)

    hash_name = sys.argv[1]
    hash_fn = get_hash(hash_name)
    digest_size = hash_fn().digest_size

    pcrs = {}

    for line in sys.stdin:
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        try:
            pcr_str, hash_hex = line.split()
            pcr = int(pcr_str)
            value = bytes.fromhex(hash_hex)
        except Exception:
            print(f"Invalid input line: {line}", file=sys.stderr)
            sys.exit(1)

        if len(value) != digest_size:
            print(f"Hash length mismatch on line: {line}", file=sys.stderr)
            sys.exit(1)

        if pcr not in pcrs:
            pcrs[pcr] = b"\x00" * digest_size

        h = hash_fn()
        h.update(pcrs[pcr] + value)
        pcrs[pcr] = h.digest()

    for pcr in sorted(pcrs):
        print(f"{pcr} {pcrs[pcr].hex()}")


if __name__ == "__main__":
    main()
