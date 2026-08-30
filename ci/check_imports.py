"""Enforces spec invariants I1-I4, I10 and I12 against a built PE binary.

Reads the import directory of a PE file and fails the build if any symbol
appears that the design forbids. This is what makes the project's safety
claim a check rather than an assertion.
"""
import sys

# I1: process memory access and remote code execution.
# I4: window hooks in any form.
# I6: synthetic input of any kind.
# I10: any networking whatsoever.
FORBIDDEN_SYMBOLS = {
    "ReadProcessMemory", "WriteProcessMemory",
    "VirtualAllocEx", "VirtualFreeEx", "VirtualProtectEx",
    "CreateRemoteThread", "CreateRemoteThreadEx",
    "NtCreateThreadEx", "QueueUserAPC",
    "SetWindowsHookExA", "SetWindowsHookExW",
    # I6: never synthesise input to the game's window.
    "SendInput", "keybd_event", "mouse_event", "BlockInput",
}
FORBIDDEN_MODULES = {"WS2_32.DLL", "WINHTTP.DLL", "WININET.DLL", "URLMON.DLL", "WINSOCK32.DLL"}


def forbidden_imports(entries):
    """entries: iterable of 'MODULE.dll!Symbol'. Returns sorted offenders."""
    bad = []
    for entry in entries:
        module, _, symbol = entry.partition("!")
        if module.upper() in FORBIDDEN_MODULES:
            bad.append(entry)
        elif symbol in FORBIDDEN_SYMBOLS:
            bad.append(entry)
    return sorted(bad)


def parse_imports(pe_path):
    """Returns a set of 'MODULE.dll!Symbol' strings from a PE import directory."""
    import pefile
    pe = pefile.PE(pe_path, fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]]
    )
    out = set()
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        module = entry.dll.decode("ascii", "replace")
        for imp in entry.imports:
            name = imp.name.decode("ascii", "replace") if imp.name else f"#{imp.ordinal}"
            out.add(f"{module}!{name}")
    return out


def check_no_elevation_manifest(pe_path):
    """I12: never request elevation. Legitimate software behaves legibly.

    The embedded manifest is a plain XML resource, so a byte scan of the image
    is sufficient and avoids a resource-directory walk that varies by linker.
    """
    with open(pe_path, "rb") as handle:
        blob = handle.read()
    return b"requireAdministrator" not in blob and b"highestAvailable" not in blob


def main(argv):
    if len(argv) < 2:
        print("usage: check_imports.py <binary> [<binary>...]", file=sys.stderr)
        return 2
    failed = False
    for path in argv[1:]:
        offenders = forbidden_imports(parse_imports(path))
        if offenders:
            failed = True
            print(f"FAIL {path}: forbidden imports (spec I1/I4/I10):", file=sys.stderr)
            for o in offenders:
                print(f"  {o}", file=sys.stderr)
        elif not check_no_elevation_manifest(path):
            failed = True
            print(f"FAIL {path}: manifest requests elevation (spec I12)", file=sys.stderr)
        else:
            print(f"ok   {path}: no forbidden imports")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
