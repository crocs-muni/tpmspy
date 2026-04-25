#!/usr/bin/env python3

import re
import sys

from typing import Optional


def parse_cpu_time(line: str) -> Optional[int]:
    m = re.search(r'(\d+)s\s+(\d+)\u03bcs', line)
    if m:
        return int(m.group(1)) * 1_000_000 + int(m.group(2))
    return None


def process_file(path: str) -> None:
    tpmspy_pid = None
    user_us = None
    system_us = None

    with open(path) as f:
        for line in f:
            m = re.match(r'tpmspy: pid (\d+)', line)

            if m:
                tpmspy_pid = m.group(1)
                continue

            if tpmspy_pid and line.startswith(f'{tpmspy_pid}'):
                if 'user CPU:' in line:
                    user_us = parse_cpu_time(line)
                elif 'system CPU:' in line:
                    system_us = parse_cpu_time(line)

    if user_us is None or system_us is None:
        print(f'{path}: Missing rusage data', file=sys.stderr)
        return

    total_us = user_us + system_us
    print(f'{path}: {total_us}\u03bcs')


for path in sys.argv[1:]:
    process_file(path)
