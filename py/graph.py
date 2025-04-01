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
        marker='o',
        markersize=10,
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


def main():
    if len(sys.argv) != 2:
        print("Usage: python script.py <input_file.json>")
        sys.exit(1)

    matplotlib_setup()
    input_file = sys.argv[1]
    with open(input_file, 'r') as f:
        data = json.load(f)

    packets = data.get("packets", [])
    if not packets:
        print("No packets found in the file.")
        return

    first_packet_time = None

    with create_subplots(figsize=(10, 5), dpi=50) as (fig, ax):
        pt_cfg: map[str, dict[str, any]] = {}

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
                    ax.axvline(x=packet_rel_time, color='red', linewidth=1)
                case _:
                    _cmd_generic(fig, ax, packet_rel_time, packet, pt_cfg)


        ax.axhline(y=0, color='black', linewidth=2)
        ax.axhline(y=8, color='black', linewidth=1.5)
        ax.set_ylim(-1.5, 16)
        #ax.set_ylim(-1.5, 24)

        # PCR Extend
        ax.set_xlim(-1.00, 10.00)
        #ax.set_xlim(32.028, 32.04)
        #ax.set_xlim(38.07, 39.05)

        # PCR Read
        #ax.set_xlim(32.028, 32.04)
        #ax.set_xlim(39.0700, 39.0830)

        ax.set_xlabel("Elapsed Time (s)")
        ax.set_ylabel("PCR Value")
        ax.set_title("PCR vs. Time")

        # Comment out these if using 'set_xlim' above!
        #ax.set_xticks(np.arange(0, 45, 5))
        ax.set_yticks(np.arange(0, 16, 2))

        ax.grid(True)
        fig.tight_layout()
        plt.show()
        #fig.savefig("fig.svg", transparent=True)


if __name__ == "__main__":
    main()
