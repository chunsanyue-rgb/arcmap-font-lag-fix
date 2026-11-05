# -*- coding: utf-8 -*-
"""
patch_iat.py -- redirect afUILib.dll's gdi32 import descriptor to afFontShim.dll

Strategy (pure byte edits, no PE re-serialization):
  1. add a new read-only section ".shim" at the end of the image holding "afFontShim.dll\\0"
  2. repoint IMAGE_IMPORT_DESCRIPTOR.Name of the gdi32 descriptor to that string
  3. zero the (ignored) CheckSum

afFontShim.dll exports every gdi32 function afUILib imports (47 passthroughs + 2 hooks),
so the whole descriptor can be retargeted without touching any thunk array.

Dry run by default. --apply writes <name>.patched.dll
"""
import argparse
import os
import sys

import pefile

SHIM_NAME = b"afFontShim.dll\x00"
FILE_ALIGN = 0x200
SECTION_ALIGN = 0x1000
SEC_CHAR = 0x40000040  # IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def add_shim_section(data, pe):
    """append a read-only section carrying the shim DLL name; returns file offset of the string"""
    hdr_room = pe.OPTIONAL_HEADER.SizeOfHeaders - (pe.sections[-1].get_file_offset() + 40)
    if hdr_room < 40:
        sys.exit("[x] no room for an extra section header (%d bytes free)" % hdr_room)

    last = max(pe.sections, key=lambda s: s.VirtualAddress)
    new_va = align(last.VirtualAddress + max(last.Misc_VirtualSize, last.SizeOfRawData), SECTION_ALIGN)
    new_raw_ptr = align(len(data), FILE_ALIGN)
    data.extend(b"\x00" * (new_raw_ptr - len(data)))
    data.extend(b"\x00" * FILE_ALIGN)

    # new section header, right after the last one
    nh = bytearray(40)
    name = b".shim\x00\x00\x00"
    nh[0:8] = name
    nh[8:12] = (len(SHIM_NAME)).to_bytes(4, "little")      # VirtualSize
    nh[12:16] = new_va.to_bytes(4, "little")               # VirtualAddress
    nh[16:20] = FILE_ALIGN.to_bytes(4, "little")           # SizeOfRawData
    nh[20:24] = new_raw_ptr.to_bytes(4, "little")          # PointerToRawData
    nh[36:40] = SEC_CHAR.to_bytes(4, "little")             # Characteristics
    hdr_off = pe.sections[-1].get_file_offset() + 40
    data[hdr_off:hdr_off + 40] = nh

    # NumberOfSections
    nsec_off = pe.FILE_HEADER.get_field_absolute_offset("NumberOfSections")
    data[nsec_off:nsec_off + 2] = (len(pe.sections) + 1).to_bytes(2, "little")
    # SizeOfImage
    soi_off = pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfImage")
    data[soi_off:soi_off + 4] = (new_va + SECTION_ALIGN).to_bytes(4, "little")
    # SizeOfInitializedData (cosmetic but keep consistent)
    sid_off = pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfInitializedData")
    old_sid = int.from_bytes(data[sid_off:sid_off + 4], "little")
    data[sid_off:sid_off + 4] = (align(old_sid + FILE_ALIGN, FILE_ALIGN)).to_bytes(4, "little")

    return new_raw_ptr, new_va


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("-o", "--out", default=None)
    args = ap.parse_args()

    with open(args.dll, "rb") as f:
        data = bytearray(f.read())

    pe = pefile.PE(data=bytes(data), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])

    target = None
    for i, entry in enumerate(pe.DIRECTORY_ENTRY_IMPORT):
        if entry.dll.lower() == b"gdi32.dll":
            target = (i, entry)
            break
    if target is None:
        sys.exit("[x] gdi32.dll import descriptor not found")

    idx, ent = target
    funcs = [f.name.decode() for f in ent.imports if f.name]
    print("[i] gdi32 descriptor #%d, %d functions" % (idx, len(funcs)))
    for fn in ("EnumFontFamiliesExW", "EnumFontFamiliesW"):
        if fn not in funcs:
            sys.exit("[x] %s not imported - wrong file?" % fn)
        print("    %-24s ok" % fn)

    file_off, rva = add_shim_section(data, pe)
    print("[i] new section .shim at rva 0x%X (file 0x%X)" % (rva, file_off))
    data[file_off:file_off + len(SHIM_NAME)] = SHIM_NAME

    desc_off = ent.struct.get_file_offset()
    old_name_rva = int.from_bytes(data[desc_off + 12:desc_off + 16], "little")
    data[desc_off + 12:desc_off + 16] = rva.to_bytes(4, "little")
    print("[i] descriptor #%d Name: 0x%X -> 0x%X" % (idx, old_name_rva, rva))

    cks_off = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    data[cks_off:cks_off + 4] = b"\x00\x00\x00\x00"
    pe.close()

    out = args.out or (os.path.splitext(args.dll)[0] + ".patched.dll")
    if not args.apply:
        print("[dry-run] computed only, nothing written (use --apply)")
        return

    with open(out, "wb") as f:
        f.write(data)

    chk = pefile.PE(out, fast_load=True)
    chk.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"],
    ])
    names = [e.dll.split(b"\x00")[0].lower() for e in chk.DIRECTORY_ENTRY_IMPORT]
    ok = b"affontshim.dll" in names
    print("[apply] %s written (%d bytes, %d sections)" % (out, len(data), len(chk.sections)))
    print("[verify] shim descriptor: %s | gdi32 descriptors left: %d"
          % ("YES" if ok else "NO", names.count(b"gdi32.dll")))
    for s in chk.sections:
        print("    section %-10s va=0x%08X vsize=0x%X" % (
            s.Name.rstrip(b"\x00").decode(errors="replace"), s.VirtualAddress, s.Misc_VirtualSize))
    chk.close()
    if not ok:
        sys.exit("[x] verification failed")


if __name__ == "__main__":
    main()
