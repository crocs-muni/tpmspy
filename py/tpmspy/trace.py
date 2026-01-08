import hashlib
import json
import struct
import yaml

from base64 import b64encode
from dataclasses import dataclass

from typing import Any
from typing import TypeAlias

from tpmspy.types import AlgName
from tpmspy.types import Digest
from tpmspy.types import DigestBank
from tpmspy.types import PCRIndex
from tpmspy.types import TimeStamp

TraceSlice: TypeAlias = list[tuple[PCRIndex, Digest]]


@dataclass
class Trace:
    extends: list[tuple[PCRIndex, DigestBank, TimeStamp]]
    digest: Digest | None

    def __init__(self, name: str) -> None:
        self.extends = []
        self.digest = None
        self.name = name

    def add(self, pcr: PCRIndex, digests: dict[AlgName, Digest] | DigestBank, timestamp: TimeStamp) -> None:
        if not isinstance(digests, DigestBank):
            digests = DigestBank(digests.items())

        self.digest = None
        self.extends.append((pcr, digests, timestamp))

    def get_digest(self) -> Digest:
        if self.digest is not None:
            return self.digest

        m = hashlib.sha1()

        for entry in self.extends:
            m.update(struct.pack("!I", entry[0]))
            for alg in sorted(entry[1]):
                m.update(entry[1][alg].encode('utf-8'))

        self.digest = b64encode(m.digest()).decode('utf-8')
        return self.digest

    def select_digests(self, alg: AlgName) -> TraceSlice:
        return [(x[0], x[1][alg]) for x in self.extends if alg in x[1]]

    def select_subset(self, algs: set[AlgName]) -> "Trace":
        trace = Trace(self.name)

        for entry in self.extends:
            digests = {alg: entry[1][alg] for alg in entry[1].keys() & algs}
            trace.add(entry[0], digests, entry[2])

        return trace

    def all_algorithms(self) -> set[AlgName]:
        out: set[AlgName] = set()

        for entry in self.extends:
            out |= entry[1].keys()

        return out

    def common_algorithms(self) -> set[AlgName]:
        out: set[AlgName] = set()

        for i, entry in enumerate(self.extends):
            if i == 0:
                out = set(entry[1].keys())
            else:
                out &= entry[1].keys()

        return out


class InvalidTraceException(Exception):
    pass


def _trace_packets_build_digests(raw_digests: list[dict[str, Any]]) -> DigestBank:
    digests: DigestBank = DigestBank()

    for entry in raw_digests:
        digests[entry["alg"]["name"].lower()] = entry["digest"]

    return digests


def trace_from_packets(name: str, packets: list[dict[str, Any]]) -> Trace:
    trace = Trace(name)
    first_packet_time: TimeStamp | None = None

    for i, packet in enumerate(packets):
        if packet['channel'] == "data" \
                and 'SOCKET_LINK_QEMU' in packet['meta']['src']['type'] \
                and packet['req'].get('name', '') == 'CMD_PCR_Extend':
            sec = packet.get("meta", {}).get("time", {}).get("sec", 0)
            usec = packet.get("meta", {}).get("time", {}).get("usec", 0)

            packet_time = sec + usec * 1e-6
            if first_packet_time is None:
                first_packet_time = packet_time

            packet_rel_time = packet_time - first_packet_time
            pcr_index = packet["attr"]["pcr-index"]
            digests = _trace_packets_build_digests(packet["attr"]["digests"])

            if pcr_index is None:
                raise InvalidTraceException(f"Packet {i}: PCR_Extend with no pcr-index attribute")

            trace.add(pcr_index, digests, packet_rel_time)

    return trace


def tpmspy_trace_open(file_name: str) -> Trace:
    try:
        with open(file_name, "r") as file:
            data = json.load(file)

        assert isinstance(data, dict)
        return trace_from_packets(file_name, data["packets"])

    except InvalidTraceException as ex:
        raise Exception(f"{file_name} contains invalid trace") from ex

    except Exception as ex:
        raise Exception(f"{file_name} cound not be loaded") from ex


def _trace_evlog_build_digests(raw_digests: list[dict[str, Any]]) -> DigestBank:
    digests: DigestBank = DigestBank()

    for entry in raw_digests:
        digests[entry["AlgorithmId"].lower()] = entry["Digest"]

    return digests


def trace_from_event_log(name: str, event_log: dict[str, Any]) -> Trace:
    assert event_log["version"] == 1

    trace = Trace(name)

    for ix, event in enumerate(event_log["events"]):
        assert ix == event["EventNum"]

        # EV_NO_ACTION must be the first event, and does not extend any PCR
        if event["EventNum"] == 0:
            if event["EventType"] != "EV_NO_ACTION":
                raise InvalidTraceException("EventNum 0 must be EV_NO_ACTION")

            continue

        digests = _trace_evlog_build_digests(event["Digests"])
        pcr_index = event["PCRIndex"]

        trace.add(pcr_index, digests, 0)

    return trace


def event_log_open(file_name: str) -> Trace:
    try:
        with open(file_name, "r") as file:
            data = yaml.load(file, Loader=yaml.Loader)

        assert isinstance(data, dict)
        return trace_from_event_log(file_name, data)

    except InvalidTraceException as ex:
        raise Exception(f"{file_name} contains invalid trace") from ex

    except Exception as ex:
        raise Exception(f"{file_name} cound not be loaded") from ex


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
