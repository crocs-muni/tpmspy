import hashlib

from typing import Any
from typing import TypeAlias

Packet: TypeAlias = dict[str, Any]
Packets: TypeAlias = list[Packet]

AlgName: TypeAlias = str
Digest: TypeAlias = str

PCRIndex: TypeAlias = int
TimeStamp: TypeAlias = int


class DigestBank(dict[AlgName, Digest]):
    def __hash__(self) -> int:  # type: ignore
        m = hashlib.sha1()

        for alg in sorted(self):
            m.update(alg.encode('utf-8'))
            m.update(self[alg].encode('utf-8'))

        return hash(m.digest())
