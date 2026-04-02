import re
import sys
from pathlib import Path


OPDEF_RE = re.compile(
    r'^OPDEF\((?P<name>[^,]+),\s*(?P<string>"[^"]+"),\s*(?P<pop>[^,]+),\s*(?P<push>[^,]+),\s*(?P<operand>[^,]+),\s*(?P<kind>[^,]+),\s*(?P<length>[^,]+),\s*(?P<byte1>[^,]+),\s*(?P<byte2>[^,]+),\s*(?P<control>[^\)]+)\)'
)


def parse_rows(source: Path):
    rows = []
    for raw_line in source.read_text(encoding="utf-8").splitlines():
        match = OPDEF_RE.match(raw_line.strip())
        if not match:
            continue

        row = {key: value.strip() for key, value in match.groupdict().items()}
        if row["length"] == "0":
            continue
        rows.append(row)
    return rows


def write_enum(rows, destination: Path):
    with destination.open("w", encoding="utf-8", newline="\n") as output:
        for row in rows:
            output.write(
                f"    {row['name']} = (((uint16_t)({row['byte1']}) == 0xFFu) ? (uint16_t)({row['byte2']}) : (uint16_t)((((uint16_t)({row['byte1']})) << 8) | (uint16_t)({row['byte2']}))),\n"
            )


def write_desc(rows, destination: Path):
    with destination.open("w", encoding="utf-8", newline="\n") as output:
        for row in rows:
            output.write(
                f"    {{ {row['name']}, {row['string']}, {row['operand']} }},\n"
            )


def main(argv):
    if len(argv) != 4:
        raise SystemExit("usage: generate_zaclr_opcode_views.py <source> <enum_out> <desc_out>")

    source = Path(argv[1])
    enum_out = Path(argv[2])
    desc_out = Path(argv[3])
    rows = parse_rows(source)
    write_enum(rows, enum_out)
    write_desc(rows, desc_out)


if __name__ == "__main__":
    main(sys.argv)
