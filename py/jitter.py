#!/usr/bin/python3

import argparse
import matplotlib
import matplotlib.pyplot as plt
import sys

from contextlib import contextmanager
from matplotlib.figure import Figure
from tpmspy.trace import tpmspy_trace_open
from typing import Any
from typing import Generator as RawGenerator
from typing import TypeAlias
from typing import TypeVar

T = TypeVar('T')
ContextManager: TypeAlias = RawGenerator[T, None, None]
Packet: TypeAlias = dict[str, dict[str, Any]]

PCRIndex: TypeAlias = int
Digest: TypeAlias = str
TimeStamp: TypeAlias = int


@contextmanager
def create_subplots(**kwargs: Any) -> ContextManager[tuple[Figure, Any]]:
    # The library sometimes complains about too many open figures, so we will
    # provide a context manager to take care of resources.
    fig, axs = plt.subplots(**kwargs)
    yield (fig, axs)
    plt.close(fig)


def matplotlib_setup() -> None:
    matplotlib.rcParams.update({
        'axes.axisbelow': True,
        'text.usetex': True,
        'font.family': 'serif',
        'font.size': 16,
        'pdf.fonttype': 42,
        'ps.fonttype': 42,
    })

    matplotlib.rcParams["text.latex.preamble"] = '\n'.join([
        r'\DeclareUnicodeCharacter{03C3}{\ensuremath{\sigma}}',
        '',
    ])


def _get_raw_data_from_file(file_name: str) -> list[tuple[PCRIndex, TimeStamp]]:
    result: list[tuple[PCRIndex, TimeStamp]] = []

    trace = tpmspy_trace_open(file_name)
    for pcr, _, ts in trace.extends:
        assert ts is not None
        result.append((pcr, ts))

    return result


def get_raw_data(files: list[str]) -> list[list[tuple[PCRIndex, TimeStamp]]]:
    return [_get_raw_data_from_file(x) for x in files]


def transpose_data(traces: list[list[tuple[PCRIndex, TimeStamp]]]) -> list[tuple[PCRIndex, list[TimeStamp]]]:
    first = traces[0]

    result: list[tuple[PCRIndex, list[TimeStamp]]] = []
    for trace_ix, trace in enumerate(traces):
        for entry_ix, (pcr, ts) in enumerate(trace):
            if trace_ix != 0 and first[entry_ix][0] != pcr:
                die(f"Trace {trace_ix} entry {entry_ix} differs from expected values: {first[entry_ix][0]=}!={pcr=}")

            if trace_ix == 0:
                result.append((pcr, [ts]))
            else:
                result[entry_ix][1].append(ts)

    return result


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plot TPM PCR_Extend jitter")
    parser.add_argument("files", nargs="+", help="Input JSON files")
    parser.add_argument("-o", "--output", default="graph.svg", help="Output file (default: graph.svg)")
    parser.add_argument("--title", default="PCR Time Jitter", help="Graph title")
    return parser


def die(s: str) -> None:
    print(s, file=sys.stderr)
    sys.exit(1)


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    if len(args.files) < 1:
        die("Too few input files")

    data = transpose_data(get_raw_data(args.files))

    matplotlib_setup()
    with create_subplots(figsize=(10, 5), dpi=50) as (fig, ax):
        ax.boxplot([x[1] for x in data],
                   tick_labels=[x[0] for x in data])
        ax.set_ylabel("Time (s)")
        ax.set_xlabel("PCR_Extend")

        ax.set_title(args.title)

        ax.grid(True)
        fig.tight_layout()

        fig.savefig(args.output, transparent=True)


if __name__ == "__main__":
    main()
