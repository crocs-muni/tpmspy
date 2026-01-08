#!/usr/bin/python3

import argparse
import json
import matplotlib
import matplotlib.colors as colors
import matplotlib.pyplot as plt
import numpy as np
import random
import sys

from contextlib import contextmanager
from matplotlib.axes import Axes
from matplotlib.figure import Figure
from typing import Any
from typing import Generator as RawGenerator
from typing import TypeAlias
from typing import TypeVar

T = TypeVar('T')
ContextManager: TypeAlias = RawGenerator[T, None, None]
Packet: TypeAlias = dict[str, dict[str, Any]]


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


def _pcr_extend(fig: Figure, ax: Axes, t: int, packet: Packet) -> None:
    pcr = packet.get("attr", {}).get("pcr-index", None)

    if pcr is None:
        return

    ax.plot(t, pcr,
            color='blue',
            alpha=0.2,
            marker='o',
            markersize=6,
            linestyle='None',
            label='PCR_Extend(' + str(pcr) + ')')


def _flatten(outer: list[list[Any]]) -> list[Any]:
    return [item for inner in outer for item in inner]


def _unique(lst: list[Any]) -> list[Any]:
    return list(set(lst))


def _pcr_read(fig: Figure, ax: Axes, t: int, packet: Packet) -> None:
    pass


def _cmd_generic_cfg(pt_cfg: dict[str, object], cmd_id: str) -> object:
    if cmd_id not in pt_cfg:
        pt_cfg[cmd_id] = {
            'plt': {
                'color': random.choice(list(colors.CSS4_COLORS.keys())),
                'marker': random.choice(list(".ov^<>8sp*hHdDPX")),
                'markersize': 5,
            },
            'scatter': random.uniform(-0.5, 0.5),
        }

    return pt_cfg[cmd_id]


def _cmd_generic(fig: Figure, ax: Axes, t: int, packet: Packet, pt_cfg: Any) -> None:
    pass


def process_packets(fig: Figure, ax: Axes, packets: list[Packet]) -> float:
    first_packet_time = None
    last_packet_time: float | None = None

    pt_cfg: dict[str, dict[str, Any]] = {}

    for packet in packets:
        sec = packet.get("meta", {}).get("time", {}).get("sec", 0)
        usec = packet.get("meta", {}).get("time", {}).get("usec", 0)

        packet_time = sec + usec * 1e-6
        if first_packet_time is None:
            first_packet_time = packet_time
        packet_rel_time = packet_time - first_packet_time

        match packet.get("req", {}).get("name", None):
            case "CMD_PCR_Extend":
                if last_packet_time is None:
                    last_packet_time = packet_rel_time
                else:
                    last_packet_time = max(last_packet_time, packet_rel_time)

                _pcr_extend(fig, ax, packet_rel_time, packet)
            case "CMD_PCR_Read":
                _pcr_read(fig, ax, packet_rel_time, packet)
            case "CMD_SET_LOCALITY":
                ax.axvline(x=packet_rel_time, color='red', linewidth=1)
            case _:
                _cmd_generic(fig, ax, packet_rel_time, packet, pt_cfg)

    assert last_packet_time is not None
    return last_packet_time


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plot TPM PCR_Extend from JSON files")
    parser.add_argument("files", nargs="+", help="Input JSON files")
    parser.add_argument("-o", "--output", default="graph.svg", help="Output file (default: graph.svg)")
    parser.add_argument("--pcrs", type=int, default=16, help="Number of PCRs to plot (1 to 24)")
    parser.add_argument("--start", type=float, default=-0.25, help="Start of the slice of the trace to display")
    parser.add_argument("--end", type=float, default=None, help="End of the slice of the trace to display")
    parser.add_argument("--title", default="PCR vs Time", help="Graph title")
    return parser


def die(s: str) -> None:
    print(s, file=sys.stderr)
    sys.exit(1)


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    if len(args.files) < 1:
        die("Too few input files")

    if not (1 <= args.pcrs <= 24):
        die("Number of PCRs must be between 1 and 24")

    if args.end is not None and args.start >= args.end:
        die("Start must be less than end")

    matplotlib_setup()
    with create_subplots(figsize=(10, 5), dpi=50) as (fig, ax):
        for file_name in args.files:
            with open(file_name, 'r') as file:
                data = json.load(file)

            if not (packets := data.get("packets", [])):
                print(f"{file_name}: No packets found")
                continue

            last_packet_time = process_packets(fig, ax, packets)

        if args.end is None:
            assert last_packet_time is not None
            args.end = last_packet_time

        ax.axhline(y=0, color='black', linewidth=2)
        ax.axhline(y=8, color='black', linewidth=1.5)

        ax.set_ylabel("PCR")
        ax.set_ylim(-1.0, args.pcrs + 1)

        ax.set_xlabel("Elapsed Time (s)")
        ax.set_xlim(args.start, args.end)

        ax.set_title(args.title)
        ax.set_yticks(np.arange(0, args.pcrs, 1))

        ax.grid(True)
        fig.tight_layout()

        fig.savefig(args.output, transparent=True)


if __name__ == "__main__":
    main()
