import pytest
from check_imports import forbidden_imports

def test_flags_process_memory_access():
    found = forbidden_imports({"KERNEL32.dll!ReadProcessMemory", "KERNEL32.dll!CreateFileW"})
    assert found == ["KERNEL32.dll!ReadProcessMemory"]

def test_flags_remote_thread_and_alloc():
    found = forbidden_imports({
        "KERNEL32.dll!CreateRemoteThread",
        "KERNEL32.dll!VirtualAllocEx",
        "KERNEL32.dll!VirtualProtectEx",
    })
    assert sorted(found) == [
        "KERNEL32.dll!CreateRemoteThread",
        "KERNEL32.dll!VirtualAllocEx",
        "KERNEL32.dll!VirtualProtectEx",
    ]

def test_flags_windows_hook():
    assert forbidden_imports({"USER32.dll!SetWindowsHookExW"}) == ["USER32.dll!SetWindowsHookExW"]

def test_flags_synthetic_input_per_i6():
    found = forbidden_imports({"USER32.dll!SendInput", "USER32.dll!mouse_event"})
    assert sorted(found) == ["USER32.dll!SendInput", "USER32.dll!mouse_event"]

def test_flags_networking_per_i10():
    found = forbidden_imports({"WS2_32.dll!socket", "WINHTTP.dll!WinHttpOpen"})
    assert sorted(found) == ["WINHTTP.dll!WinHttpOpen", "WS2_32.dll!socket"]

def test_allows_legitimate_imports():
    assert forbidden_imports({
        "USER32.dll!SetWinEventHook",
        "USER32.dll!GetWindowLongW",
        "KERNEL32.dll!OpenProcess",
        "d3d12.dll!D3D12CreateDevice",
        "dcomp.dll!DCompositionCreateDevice",
    }) == []
