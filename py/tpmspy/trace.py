import hashlib
import struct

from base64 import b64encode
from dataclasses import dataclass

from typing import Any

from tpmspy.types import Digest
from tpmspy.types import PCRIndex
from tpmspy.types import TimeStamp


@dataclass
class Trace:
    extend: list[tuple[PCRIndex, Digest, TimeStamp | None]]
    digest: Digest | None

    def __init__(self, name: str) -> None:
        self.extend = []
        self.digest = None
        self.name = name

    def add(self, pcr: PCRIndex, digest: Digest, timestamp: TimeStamp) -> None:
        self.extend.append((pcr, digest, timestamp))

    def get_digest(self) -> Digest:
        if self.digest is not None:
            return self.digest

        m = hashlib.sha1()

        for entry in self.extend:
            m.update(struct.pack("!I", entry[0]))
            m.update(entry[1].encode('utf-8'))

        self.digest = b64encode(m.digest()).decode('utf-8')
        return self.digest


class InvalidTraceException(Exception):
    pass


def _trace_add_packet(trace: Trace, extend: Any, alg: str, timestamp: TimeStamp) -> None:
    pcr: PCRIndex | None = extend['pcr-index']

    if pcr is None:
        raise InvalidTraceException("PCR_Extend with no pcr-index attribute")

    for entry in extend['digests']:
        if entry['alg']['name'] == alg:
            trace.add(pcr, entry['digest'], timestamp)
            return

    raise InvalidTraceException(f"PCR_Extend with no {alg} digest")


def trace_from_packets(name: str, packets: list[dict[str, Any]], alg: str) -> Trace:
    trace = Trace(name)
    first_packet_time: TimeStamp | None = None

    for packet in packets:
        if packet['channel'] == "data" \
                and 'SOCKET_LINK_QEMU' in packet['meta']['src']['type'] \
                and packet['req'].get('name', '') == 'CMD_PCR_Extend':
            sec = packet.get("meta", {}).get("time", {}).get("sec", 0)
            usec = packet.get("meta", {}).get("time", {}).get("usec", 0)

            packet_time = sec + usec * 1e-6
            if first_packet_time is None:
                first_packet_time = packet_time
            packet_rel_time = packet_time - first_packet_time
            _trace_add_packet(trace, packet['attr'], alg, packet_rel_time)

    return trace


def _trace_add_event(trace: Trace, event: dict[str, Any], alg: str) -> None:
    for digest in event["Digests"]:
        if digest["AlgorithmId"] == alg.lower():
            trace.add(event["PCRIndex"], digest["Digest"], 0)
            return

    raise InvalidTraceException(f"EventNum {event["EventNum"]} has no digest {alg.lower()}")


def trace_from_event_log(name: str, event_log: dict[str, Any], alg: str) -> Trace:
    assert event_log["version"] == 1

    trace = Trace(name)

    for ix, event in enumerate(event_log["events"]):
        assert ix == event["EventNum"]

        # EV_NO_ACTION must be the first event, and does not extend any PCR
        if event["EventNum"] == 0:
            if event["EventType"] != "EV_NO_ACTION":
                raise InvalidTraceException("EventNum 0 must be EV_NO_ACTION")

            continue

        _trace_add_event(trace, event, alg)

    return trace


@dataclass
class TraceList:
    @dataclass
    class TraceListEntry:
        trace: Trace
        count: int

        def __init__(self, trace: Trace):
            self.trace = trace
            self.count = 0

        def inc(self) -> None:
            self.count += 1

    traces: dict[str, TraceListEntry]

    def __init__(self) -> None:
        self.traces = {}

    def add_trace(self, trace: Trace) -> None:
        digest = trace.get_digest()

        if digest not in self.traces:
            self.traces[digest] = TraceList.TraceListEntry(trace)

        self.traces[digest].inc()
