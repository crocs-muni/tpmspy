#!/bin/env python3

import argparse
import json
import matplotlib
import matplotlib.colors as colors
import matplotlib.pyplot as plt
import numpy as np
import random
import sys

from contextlib import contextmanager
from typing import Any
from typing import Generator as RawGenerator
from typing import TypeAlias
from typing import TypeVar

T = TypeVar('T')
ContextManager: TypeAlias = RawGenerator[T, None, None]


@contextmanager
def create_subplots(**kwargs: Any) -> ContextManager[tuple[plt.Figure, Any]]:
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


def _pcr_extend(fig, ax, t, packet):
    pcr = packet.get("attr", {}).get("pcr-index", None)

    if pcr is None:
        return

    ax.plot(t, pcr,
        color='r',
        marker='o',
        markersize=10,
        linestyle='None',
        label='PCR_Extend(' + str(pcr) + ')'
    )


def _flatten(outer: [[any]]) -> [any]:
    return [item for inner in outer for item in inner]


def _unique(l: [any]) -> [any]:
    return list(set(l))


def _pcr_read(fig, ax, t, packet):
    pcr_selections = map(lambda s: s.get("pcr-index", []), packet.get("attr", {}).get("selections", []))
    pcrs = _unique(_flatten(pcr_selections))

    if pcrs == []:
        return

    ax.plot([t for _ in pcrs], pcrs,
        color='g',
        marker='+',
        markersize=14,
        linestyle='-',
        label='PCR_Read(' + str(pcrs) + ')'
    )


def _cmd_generic_cfg(pt_cfg, cmd_id):
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


def _cmd_generic(fig, ax, t, packet, pt_cfg):
    cmd_id = packet.get("req", {}).get("code", 0)
    cmd_name = packet.get("req", {}).get("name", "Unknown")

    cfg = _cmd_generic_cfg(pt_cfg, cmd_id)
    ax.plot(t, -1 + cfg["scatter"],
            **(cfg["plt"]),
            label=cmd_name,
    )


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plot TPM PCR_Extend and PCR_Read from a JSON file")
    parser.add_argument("file", help="Input JSON file")
    parser.add_argument("-o", "--output", default="graph.svg", help="Output file")
    parser.add_argument("--all-events", help="Show all events below the graph using random symbols")
    parser.add_argument("--pcrs", type=int, default=16, help="Number of PCRs to plot (1 to 24)")
    parser.add_argument("--start", type=float, default=-1.0, help="Start of the slice of the trace to display")
    parser.add_argument("--end", type=float, default=3.0, help="End of the slice of the trace to display")
    parser.add_argument("--title", default="PCR vs Time", help="Graph title")
    return parser


def die(s: str) -> None:
    print(s, file=sys.stderr)
    sys.exit(1)


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    if not (1 <= args.pcrs <= 24):
        die("Number of PCRs must be between 1 and 24")

    if args.start >= args.end:
        die("Start must be less than end")

    matplotlib_setup()
    with open(args.file, 'r') as f:
        data = json.load(f)

    packets = data.get("packets", [])
    if not packets:
        print("No packets found in the file.")
        return

    first_packet_time = None

    with create_subplots(figsize=(10, 5), dpi=50) as (fig, ax):
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
                    _pcr_extend(fig, ax, packet_rel_time, packet)
                case "CMD_PCR_Read":
                    _pcr_read(fig, ax, packet_rel_time, packet)
                case "CMD_SET_LOCALITY":
                    ax.axvline(x=packet_rel_time, color='orange', linewidth=0.5)
                case _:
                    if args.all_events:
                        _cmd_generic(fig, ax, packet_rel_time, packet, pt_cfg)


        ax.axhline(y=0, color='black', linewidth=2)
        ax.axhline(y=8, color='black', linewidth=1.5)

        ax.set_ylabel("PCR")
        ax.set_ylim(-1.0, args.pcrs)

        ax.set_xlabel("Elapsed Time (s)")
        ax.set_xlim(args.start, args.end)

        ax.set_title(args.title)

        # Comment out these if using 'set_xlim' above!
        #ax.set_xticks(np.arange(0, 45, 5))
        ax.set_yticks(np.arange(0, 16, 1))

        ax.grid(True)
        fig.tight_layout()
        fig.savefig(args.output, transparent=True)


if __name__ == "__main__":
    main()
