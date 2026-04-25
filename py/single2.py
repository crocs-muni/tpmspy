#!/usr/bin/python3

import argparse
import matplotlib
import matplotlib.colors as colors
import matplotlib.pyplot as plt
import json
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

from tpmspy.trace import event_log_open
from tpmspy.trace import tpmspy_trace_open
from tpmspy.trace import Trace
from tpmspy.trace import TraceSlice
from tpmspy.types import AlgName
from tpmspy.types import Digest
from tpmspy.types import DigestBank
from tpmspy.types import PCRIndex
from tpmspy.types import TimeStamp

T = TypeVar('T')
ContextManager: TypeAlias = RawGenerator[T, None, None]
Packet: TypeAlias = dict[str, dict[str, Any]]
MatchedEvents: TypeAlias = list[tuple[PCRIndex, TimeStamp, bool]]


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
        'font.size': 20,
        'pdf.fonttype': 42,
        'ps.fonttype': 42,
    })

    matplotlib.rcParams["text.latex.preamble"] = '\n'.join([
        r'\DeclareUnicodeCharacter{03C3}{\ensuremath{\sigma}}',
        '',
    ])


def _pcr_extend(fig: Figure, ax: Axes, t: tuple[PCRIndex, TimeStamp, bool]) -> None:
    if t[2]:
        ax.plot(t[1], t[0],
                color='#2e8b57',
                marker='o',
                markersize=8,
                linestyle='None',
                label='PCR_Extend(' + str(t[0]) + ')')
    else:
        ax.plot(t[1], t[0],
                marker='o',
                markersize=6,
                markeredgecolor="#b5223b",
                markerfacecolor="none",
                markeredgewidth=2,
                linestyle='None',
                label='PCR_Extend(' + str(t[0]) + ')')


def _pcr_read(fig: Figure, ax: Axes, t: tuple[PCRIndex, TimeStamp]) -> None:
    ax.plot(t[1], t[0],
            color='g',
            marker='+',
            markersize=14,
            linestyle='-',
            label='PCR_Read(' + str(t[0]) + ')')


def _cmd_generic_cfg(pt_cfg: dict[str, Any], cmd_id: str) -> Any:
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


def _cmd_generic(fig: Figure, ax: Axes, t: int, packet: Packet, pt_cfg: dict[str, Any]) -> None:
    cmd_id = packet.get("req", {}).get("code", 0)
    cmd_name = packet.get("req", {}).get("name", "Unknown")

    cfg = _cmd_generic_cfg(pt_cfg, cmd_id)
    ax.plot(t, -1 + cfg["scatter"],
            **(cfg["plt"]),
            label=cmd_name,
            )


def _flatten(outer: list[list[Any]]) -> list[Any]:
    return [item for inner in outer for item in inner]


def _unique(lst: list[Any]) -> list[Any]:
    return list(set(lst))


def select_algorithm(args: argparse.Namespace, a: set[AlgName], b: set[AlgName]) -> AlgName:
    comb = a & b

    if len(comb) == 0:
        die(f"No common algorithm exists: {a=}, {b=}")

    if args.algorithm is not None:
        alg = args.algorithm

        if alg not in comb:
            die(f"Selected algorithm {args.algorithm} is not supported: {a=} {b=}")

        assert isinstance(alg, str)
        return alg

    if len(comb) > 1:
        die(f"Choose an algorithm using '--algorithm NAME' from {comb}")

    return next(iter(comb))


def find_extend(checklist: list[list[tuple[PCRIndex, Digest] | bool]], alg: AlgName, extend: tuple[PCRIndex, DigestBank, TimeStamp]) -> tuple[PCRIndex, Digest] | None:
    needle = (extend[0], extend[1][alg])

    found_used: list[tuple[PCRIndex, Digest] | bool] | None = None
    for e in checklist:
        if e[0] == needle:
            if e[1]:
                found_used = e
                continue

            e[1] = True

            (v, _) = e
            assert isinstance(v, tuple)
            return v

    if found_used is not None:
        warn(f"Event {needle} selected multiple times")
        (v, _) = found_used
        assert isinstance(v, tuple)
        return v

    return None


def match_events(alg: AlgName, tpmspy: Trace, tpmlog: TraceSlice) -> MatchedEvents:
    result: MatchedEvents = []

    checklist: list[list[tuple[PCRIndex, Digest] | bool]] = []
    for entry in tpmlog:
        checklist.append([entry, False])

    for extend in tpmspy.extends:
        tpmlog_extend = find_extend(checklist, alg, extend)
        result.append((extend[0], extend[2], tpmlog_extend is not None))

    # Sanity check: Were all events from checklist processed?
    remains = list(filter(lambda todo: not todo[1], checklist))
    if len(remains) > 0:
        print("Events missing in TPMSpy Log were detected:", file=sys.stderr)

        for (e, _) in remains:
            assert isinstance(e, tuple)
            print(f"PCR_Extend index:{e[0]} digest:{e[1]}", file=sys.stderr)

        warn("BUG: TPMSpy Log missed some events (see above)")

    return result


def legacy_packets(filename: str) -> list[tuple[Any, Any]] | None:
    with open(filename, 'r') as f:
        data = json.load(f)

    packets = data.get("packets", [])
    if not packets:
        print("No packets found in the file.")
        return None

    first_packet_time = None
    last_packet_time: float | None = None

    result = []
    for packet in packets:
        sec = packet.get("meta", {}).get("time", {}).get("sec", 0)
        usec = packet.get("meta", {}).get("time", {}).get("usec", 0)

        packet_time = sec + usec * 1e-6
        if first_packet_time is None:
            first_packet_time = packet_time
        packet_rel_time = packet_time - first_packet_time

        if last_packet_time is None:
            last_packet_time = packet_rel_time
        else:
            last_packet_time = max(last_packet_time, packet_rel_time)

        result.append((packet_rel_time, packet))

    return result


def process_legacy_packets(args: argparse.Namespace, fig: Any, ax: Any, packets: list[tuple[Any, Any]]) -> None:
    pt_cfg: dict[str, dict[str, Any]] = {}

    for (stamp, packet) in packets:
        match packet.get("req", {}).get("name", None):
            case "CMD_PCR_Read":
                if args.pcr_read:
                    pcr_selections = list(map(lambda s: s.get("pcr-index", []), packet.get("attr", {}).get("selections", [])))
                    pcrs = _unique(_flatten(pcr_selections))

                    if pcrs == []:
                        continue

                    for pcr in pcrs:
                        _pcr_read(fig, ax, (pcr, stamp))
            case "CMD_SET_LOCALITY":
                ax.axvline(x=stamp, color='orange', linewidth=0.5)
            case _:
                if args.all_events:
                    _cmd_generic(fig, ax, stamp, packet, pt_cfg)


def get_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plot TPM PCR_Extend and PCR_Read from a JSON file")
    parser.add_argument("tpmspy", help="TPMSpy JSON Log")
    parser.add_argument("tpmlog", help="TPM2 Event Log")
    parser.add_argument("-o", "--output", default="graph.svg", help="Output file")
    parser.add_argument("-A", "--algorithm", type=str, help="Select algorithm to plot")
    parser.add_argument("--all-events", help="Show all events below the graph using random symbols")
    parser.add_argument("--pcr-read", help="Include PCR_Read operation as a cross")
    parser.add_argument("--pcrs", type=int, default=16, help="Number of PCRs to plot (1 to 24)")
    parser.add_argument("--start", type=float, default=-1.0, help="Start of the slice of the trace to display")
    parser.add_argument("--end", type=float, default=None, help="End of the slice of the trace to display")
    parser.add_argument("--title", default="PCR vs Time", help="Graph title")
    return parser


def die(s: str) -> None:
    print(s, file=sys.stderr)
    sys.exit(1)


def warn(s: str) -> None:
    print(s, file=sys.stderr)


def main() -> None:
    argp = get_arg_parser()
    args = argp.parse_args()

    if not (1 <= args.pcrs <= 24):
        die("Number of PCRs must be between 1 and 24")

    if args.end is not None and args.start >= args.end:
        die("Start must be less than end")

    tpmspy = tpmspy_trace_open(args.tpmspy)
    tpmlog = event_log_open(args.tpmlog)

    algs_tpmspy = tpmspy.all_algorithms()
    algs_tpmlog = tpmlog.all_algorithms()

    alg = select_algorithm(args, algs_tpmspy, algs_tpmlog)
    data = match_events(alg, tpmspy, tpmlog.select_digests(alg))

    if args.end is None:
        args.end = max(map(lambda e: e[2], tpmspy.extends)) + 1.0

    matplotlib_setup()

    with create_subplots(figsize=(10, 5), dpi=50) as (fig, ax):
        for event in data:
            _pcr_extend(fig, ax, event)

        packets = legacy_packets(args.tpmspy)

        if packets is not None:
            process_legacy_packets(args, fig, ax, packets)

        ax.axhline(y=0, color='black', linewidth=2)
        ax.axhline(y=8, color='black', linewidth=1.5)

        ax.set_ylabel("PCR")
        ax.set_ylim(-1.0, args.pcrs)

        ax.set_xlabel("Elapsed Time (s)")
        ax.set_xlim(args.start, args.end)

        ax.set_title(args.title)
        ax.set_yticks(np.arange(0, 16, 1))

        ax.grid(True)
        fig.tight_layout()
        fig.savefig(args.output, transparent=True)


if __name__ == "__main__":
    main()
