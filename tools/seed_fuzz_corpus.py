#!/usr/bin/env python3
"""Seeds tests/fuzz/corpus/ from the real C# MemoryPack fixtures.

An empty corpus makes libFuzzer spend a long time rediscovering a valid object
header from scratch. tests/fixtures/*.bin already contains 53 real MemoryPack
outputs, so this script turns each relevant one into a seed by prepending the
one-byte type selector that fuzz_deserialize.cpp's LLVMFuzzerTestOneInput
switches on.

The selector -> type mapping below MUST stay in sync with the
`switch (selector % 12)` in tests/fuzz/fuzz_deserialize.cpp.

Usage:
    python tools/seed_fuzz_corpus.py

Idempotent: re-running produces byte-identical output, so there is nothing to
gitignore and nothing to diff after a second run.
"""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES_DIR = ROOT / "tests" / "fixtures"
CORPUS_DIR = ROOT / "tests" / "fuzz" / "corpus"

# selector -> list of fixture basenames (relative to tests/fixtures/) to seed
# with that selector. Selector 10 (raw reader) is fed every fixture, since it
# drives the low-level reader API directly rather than one generated type.
ALL_FIXTURES = sorted(p.name for p in FIXTURES_DIR.glob("*.bin"))

SELECTOR_FIXTURES: dict[int, list[str]] = {
    0: ["simple_packet.bin"],                                   # Item
    1: ["inventory.bin", "dict_object_packet.bin"],             # Inventory
    2: ["all_primitives.bin", "many_members.bin"],              # Everything
    3: ["nested_object.bin", "nested_list.bin"],                # Deep
    4: sorted(p.name for p in FIXTURES_DIR.glob("string_top_level*.bin")),  # std::string
    5: ["int_list_top_level.bin", "int_list_empty.bin", "int_list_null.bin"],  # vector<int32>
    6: ["array_packet.bin"],                                    # vector<string>
    7: ["dict_packet.bin"],                                     # map<string,string>
    8: sorted(p.name for p in FIXTURES_DIR.glob("union_top_level*.bin")),   # variant
    9: sorted(p.name for p in FIXTURES_DIR.glob("nullable_managed_holder*.bin")),  # optional<Item>
    10: ALL_FIXTURES,                                           # raw reader API
    11: ["simple_packet.bin"],                                  # frame parser
}


def main() -> int:
    if not FIXTURES_DIR.is_dir():
        print(f"seed_fuzz_corpus: no such directory: {FIXTURES_DIR}", file=sys.stderr)
        return 1

    CORPUS_DIR.mkdir(parents=True, exist_ok=True)

    written = 0
    for selector, names in SELECTOR_FIXTURES.items():
        for name in names:
            src = FIXTURES_DIR / name
            if not src.is_file():
                print(f"seed_fuzz_corpus: missing fixture {src}", file=sys.stderr)
                return 1
            payload = bytes([selector]) + src.read_bytes()
            stem = pathlib.Path(name).stem
            dst = CORPUS_DIR / f"{stem}__sel{selector}.bin"
            dst.write_bytes(payload)
            written += 1

    print(f"seed_fuzz_corpus: wrote {written} seed file(s) to {CORPUS_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
