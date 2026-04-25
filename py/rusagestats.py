#!/usr/bin/env python3

import sys
import statistics

from typing import Tuple
from typing import TextIO


def read_numbers(source: TextIO) -> [int]:
    return [float(x) for x in source.read().split()]


def summarize(data: [int]) -> Tuple[float, float]:
    if len(data) == 0:
        return float('Nan'), float('NaN')

    mean = statistics.mean(data)

    if len(data) > 1:
        std = statistics.stdev(data)
    else:
        std = 0.0

    return mean, std


def main() -> None:
    mean, std = summarize(read_numbers(sys.stdin))
    print(f'{mean:.2f}±{std:.2f}')


if __name__ == "__main__":
    main()
