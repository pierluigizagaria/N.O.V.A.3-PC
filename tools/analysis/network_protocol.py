"""Inventory native online wire metadata. Original inputs and reports stay local.

This development tool emits sizes, symbol/callsite references and handler
entrypoints, never executable instructions. Static builder references are
candidates for review, not inferred schemas or implemented server features.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from elftools.elf.elffile import ELFFile

IDENTITY = "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e"


def request_candidates(instructions, read, got):
    """Find original HTTP format references without pretending to emulate x86.

    Immediate selectors are evidence only when the format starts with f/g and
    the same basic block writes sprintf's first variadic argument. Dynamic and
    branch-dependent arguments remain unknown; never infer them from names.
    """
    result = []
    for index, instruction in enumerate(instructions):
        if instruction.mnemonic != 'lea':
            continue
        match = re.fullmatch(r'(\w+), \[ebx ([+-]) (0x[0-9a-f]+)\]', instruction.op_str)
        if not match:
            continue
        address = got + int(match[3], 0) * (1 if match[2] == '+' else -1)
        try:
            raw = read(address, 1024).split(b'\0', 1)[0]
            value = raw.decode('ascii')
        except (ValueError, UnicodeError):
            continue
        if not re.search(r'(?:^|[?&|])[fg][=|]', value):
            continue
        selector = re.search(r'(?:^|[?&|])([fg])[=|](\d+|%[diu])(?:[&|]|$)', value)
        item = {'template': value, 'reference': f'0x{instruction.address:x}'}
        if selector:
            item['selector'] = selector[1]
            if selector[2].isdigit():
                item['function'] = int(selector[2])
            elif selector.start() == 0:
                # The reference can precede or follow the argument assignment.
                start = max(0, index-20)
                for at in range(index-1, start-1, -1):
                    if instructions[at].mnemonic == 'call':
                        start = at+1
                        break
                end = min(len(instructions), index+14)
                for at in range(index+1, end):
                    if instructions[at].mnemonic == 'call':
                        end = at
                        break
                candidates = instructions[start:end]
                assignments = []
                for previous in candidates:
                    if previous.mnemonic == 'mov' and previous.op_str.startswith('dword ptr [esp + 8], '):
                        number = previous.op_str.split(', ', 1)[1]
                        if re.fullmatch(r'(?:0x[0-9a-f]+|\d+)', number):
                            assignments.append(int(number, 0))
                if len(set(assignments)) == 1:
                    item['function_candidate'] = assignments[0]
        result.append(item)
    return result


def inventory(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != IDENTITY:
        raise ValueError("Unsupported native library")
    with path.open("rb") as source:
        elf = ELFFile(source)
        symbols = list(elf.get_section_by_name(".symtab").iter_symbols())

        def read(address, count):
            for segment in elf.iter_segments():
                if (segment["p_type"] == "PT_LOAD"
                        and segment["p_vaddr"] <= address
                        and address + count <= segment["p_vaddr"] + segment["p_filesz"]):
                    source.seek(segment["p_offset"] + address - segment["p_vaddr"])
                    return source.read(count)
            raise ValueError(f"Address outside file: {address:x}")

        tables = [s for s in symbols if s.name == "_ZL19MP_MESSAGES_LENGTHS"]
        if not tables or any(s["st_size"] != 660 for s in tables):
            raise ValueError("Unexpected framing table")
        sizes = [list(struct.unpack("<165I", read(s["st_value"], 660))) for s in tables]
        if any(table != sizes[0] for table in sizes):
            raise ValueError("Framing tables disagree")
        records = [{"id": i, "size": "variable" if n == 0xffffff else n,
                    "builder_candidates": []} for i, n in enumerate(sizes[0])]
        got = 0x113e340
        for record in records[16:]:
            offset = struct.unpack("<i", read(got - 0x2b0c94 + (record["id"] - 16) * 4, 4))[0]
            record["game_handler"] = f"0x{(got + offset) & 0xffffffff:x}"
        cs = Cs(CS_ARCH_X86, CS_MODE_32)
        funcs = {s["st_value"]: s for s in symbols
                 if s["st_info"]["type"] == "STT_FUNC" and s["st_size"]}
        for symbol in funcs.values():
            if not any(name in symbol.name for name in ("MpManager", "Comms")):
                continue
            instructions = list(cs.disasm(read(symbol["st_value"], symbol["st_size"]), symbol["st_value"]))
            for index, instruction in enumerate(instructions):
                if instruction.mnemonic != "call" or instruction.op_str != "0x420230":
                    continue
                for previous in reversed(instructions[max(0, index - 12):index]):
                    if previous.mnemonic == "mov" and previous.op_str.startswith("dword ptr [esp + 4], "):
                        value = previous.op_str.split(", ", 1)[1]
                        if re.fullmatch(r"(?:0x[0-9a-f]+|[0-9]+)", value):
                            kind = int(value, 0)
                            if 0 < kind < len(records):
                                records[kind]["builder_candidates"].append({
                                    "symbol": symbol.name, "callsite": f"0x{instruction.address:x}"})
                        break
        families = {}
        http = []
        for family in ("ConnectionLobby", "XPlayerLeaderBoard", "GLXPlayerUserFriend",
                       "GLXPlayerUser", "GLXPlayerMessage", "GLXPlayerLogin",
                       "CSeshat", "GLonlineLib12EveComponent"):
            families[family] = [{"symbol": s.name, "address": f"0x{s['st_value']:x}"}
                                for s in funcs.values() if family in s.name]
        for symbol in funcs.values():
            if not any(family in symbol.name for family in families):
                continue
            instructions = list(cs.disasm(read(symbol['st_value'], symbol['st_size']), symbol['st_value']))
            candidates = request_candidates(instructions, read, got)
            if candidates:
                http.append({'symbol': symbol.name, 'requests': candidates})
        lobby_dispatch = []
        for opcode in range(149):
            offset = struct.unpack('<i', read(got - 0x283c24 + opcode*4, 4))[0]
            lobby_dispatch.append({'opcode': opcode, 'handler': f'0x{(got+offset) & 0xffffffff:x}'})
        return {"library_sha256": IDENTITY, "game_records": records,
                "online_symbols": families,
                "lobby_dispatch": lobby_dispatch, "http_request_candidates": http,
                "note": "Structural inventory only; semantics and authority require separate review."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to(root / "local/analysis"):
        parser.error("Original-library research output must stay under local/analysis")
    result = inventory(args.library)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Inventoried {len(result['game_records'])-1} native game records; output: {output}")


if __name__ == "__main__":
    main()
