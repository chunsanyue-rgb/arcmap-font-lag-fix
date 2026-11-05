# -*- coding: utf-8 -*-
"""
patch_exports.py -- rewrite shim DLL export names in place:
    _w_GetTextColor@4          -> GetTextColor
    _w_EnumFontFamiliesExW@20  -> EnumFontFamiliesExW

Same-length replacement (name + NUL padding) so no offset shifts anywhere.
"""
import sys
import pefile

path = sys.argv[1] if len(sys.argv) > 1 else "afFontShim.dll"
data = bytearray(open(path, "rb").read())
pe = pefile.PE(data=bytes(data))
pe.parse_data_directories()
if not hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
    sys.exit("[x] no export directory")

changed = []
for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
    if not sym.name:
        continue
    old = sym.name.decode()
    new = old[1:] if old.startswith("_") else old
    if new.startswith("w_"):
        new = new[2:]
    if "@" in new:
        new = new.split("@")[0]
    if new == old:
        continue
    if len(new) > len(old):
        print("[!] skip (would grow):", old)
        continue
    needle = old.encode() + b"\x00"
    idx = data.find(needle)
    if idx < 0:
        print("[!] not found in image:", old)
        continue
    repl = new.encode() + b"\x00" * (len(needle) - len(new))
    data[idx:idx + len(needle)] = repl
    changed.append((old, new))
pe.close()

open(path, "wb").write(data)

chk = pefile.PE(path)
chk.parse_data_directories()
names = sorted(e.name.decode() for e in chk.DIRECTORY_ENTRY_EXPORT.symbols if e.name)
print("[ok] renamed %d exports" % len(changed))
print("[i] exports now (%d): %s" % (len(names), ", ".join(names)))
missing = [n for n in ("EnumFontFamiliesExW", "EnumFontFamiliesW", "GetTextMetricsW",
                       "CreateFontIndirectW", "GetFontData", "SelectObject") if n not in names]
print("[verify] key names missing:", missing if missing else "none")
chk.close()
