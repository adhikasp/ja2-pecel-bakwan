"""Reader for SLF archives (Sir-tech Library File), as the game reads them.

Layout (see rust/stracciatella/src/file_formats/slf.rs and LibraryDatabase):
  header, 532 bytes: name[256], path[256], int32 entry count (offset 512), int32 ok count,
                     uint16 sort, uint16 version, uint8 contains-subdirs, 3 pad, int32 reserved
  data
  entries at the end of the file, 280 bytes each: name[256], uint32 offset, uint32 length, uint8 state
                     (0 = ok, 1 = old: superseded, not used by the game), uint8 reserved, 2 pad,
                     FILETIME (8), uint16 reserved, 2 pad
Strings are NUL terminated, paths use backslashes and are relative to the library path.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator

HEADER_BYTES = 532
ENTRY_BYTES = 280
STATE_OK = 0


@dataclass(frozen=True)
class SlfEntry:
    name: str        # path inside the library, forward slashes
    offset: int
    length: int
    state: int

    @property
    def ok(self) -> bool:
        return self.state == STATE_OK


def _cstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("latin-1")


class SlfArchive:
    """An open SLF file. `path` is the library path (e.g. "Interface/") the entries are relative to."""

    def __init__(self, file: str | Path):
        self.file = Path(file)
        self._f: BinaryIO = open(self.file, "rb")
        header = self._f.read(HEADER_BYTES)
        if len(header) != HEADER_BYTES:
            raise ValueError(f"{self.file}: too short for an SLF header")
        self.name = _cstr(header[0:256])
        self.path = _cstr(header[256:512]).replace("\\", "/")
        (count,) = struct.unpack_from("<i", header, 512)
        if count < 0:
            raise ValueError(f"{self.file}: negative entry count")
        self._f.seek(0, 2)
        size = self._f.tell()
        table = size - count * ENTRY_BYTES
        if table < HEADER_BYTES:
            raise ValueError(f"{self.file}: entry table does not fit ({count} entries)")
        self._f.seek(table)
        raw = self._f.read(count * ENTRY_BYTES)
        self.entries: list[SlfEntry] = []
        for i in range(count):
            e = raw[i * ENTRY_BYTES:(i + 1) * ENTRY_BYTES]
            offset, length, state = struct.unpack_from("<IIB", e, 256)
            if offset < HEADER_BYTES or offset + length > table:
                raise ValueError(f"{self.file}: entry {i} points outside the data")
            self.entries.append(SlfEntry(_cstr(e[:256]).replace("\\", "/"), offset, length, state))

    def close(self) -> None:
        self._f.close()

    def __enter__(self) -> "SlfArchive":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def files(self) -> Iterator[SlfEntry]:
        """The entries the game uses (state ok)."""
        return (e for e in self.entries if e.ok)

    def read(self, entry: SlfEntry) -> bytes:
        self._f.seek(entry.offset)
        return self._f.read(entry.length)

    def full_path(self, entry: SlfEntry) -> str:
        """Path relative to the Data directory, as the VFS sees it (e.g. "Interface/ADDONSLCP.STI")."""
        return self.path + entry.name


def write_slf(file: str | Path, name: str, lib_path: str, files: dict[str, bytes]) -> None:
    """Writes a minimal SLF (used by the tests to make synthetic archives)."""
    header = bytearray(HEADER_BYTES)
    header[0:len(name)] = name.encode("latin-1")
    lp = lib_path.replace("/", "\\").encode("latin-1")
    header[256:256 + len(lp)] = lp
    struct.pack_into("<iiHHB", header, 512, len(files), len(files), 0xFFFF, 0x0200, 0)
    data = bytearray()
    table = bytearray()
    for fname, content in files.items():
        e = bytearray(ENTRY_BYTES)
        fn = fname.replace("/", "\\").encode("latin-1")
        e[0:len(fn)] = fn
        struct.pack_into("<IIB", e, 256, HEADER_BYTES + len(data), len(content), STATE_OK)
        table += e
        data += content
    Path(file).write_bytes(bytes(header) + bytes(data) + bytes(table))
