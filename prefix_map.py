"""
PlatformIO Pre-Build Script — OPSEC path scrub.

The ESP32 framework's assert/HAL/log macros bake __FILE__ into the binary, which
embeds the local build path — and with it the machine's username — into
firmware.bin. This rewrites those path prefixes to neutral tokens at compile time
via -ffile-prefix-map, so firmware.bin carries no personal path.

Prefixes are computed at build time (expanduser / PlatformIO dirs), so no personal
path is ever written into a tracked file. The toolchain emits paths in several
forms on Windows (back- vs forward-slash, and an occasional leading slash), so each
prefix is mapped in all of them. Most-specific prefixes are listed first because
-ffile-prefix-map applies the first matching rule.
"""

import os

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)


def _forms(path):
    """All separator / leading-slash spellings of a path prefix."""
    p = path.rstrip("/\\")
    base = {p, p.replace("\\", "/"), p.replace("/", "\\")}
    base |= {"/" + x for x in list(base)}  # toolchain sometimes prepends '/'
    return base


# (prefix, replacement) — specific dirs before the broad home dir (first match wins).
_bases = [
    (env.subst("$PROJECT_CORE_DIR"), "framework"),  # ...\.platformio
    (env.subst("$PROJECT_DIR"), "prj"),             # the repo checkout
    (os.path.expanduser("~"), "u"),                 # catch-all home dir
]

_seen = set()
_flags = []
for _base, _repl in _bases:
    if not _base:
        continue
    for _f in _forms(_base):
        if _f in _seen:
            continue
        _seen.add(_f)
        _flags.append("-ffile-prefix-map=%s=%s" % (_f, _repl))

# Apply to every compile step (C, C++, assembler).
env.Append(CCFLAGS=_flags, CXXFLAGS=_flags, ASFLAGS=_flags)
print("[PREFIX-MAP] stripping %d local path prefix forms from __FILE__" % len(_flags))
