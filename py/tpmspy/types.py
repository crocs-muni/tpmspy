from typing import Any
from typing import TypeAlias

Packet: TypeAlias = dict[str, Any]
Packets: TypeAlias = list[Packet]

Digest: TypeAlias = str
PCRIndex: TypeAlias = int
TimeStamp: TypeAlias = int
