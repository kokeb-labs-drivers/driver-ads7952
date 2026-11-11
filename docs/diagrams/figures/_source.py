"""Shared helpers for the ADS7952 figures: read constants and function bodies from the repository.

Every helper raises when the code no longer looks the way a figure expects, so a figure fails to build instead of
drawing something the code does not do.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REGISTERS = "inc/ads7952_registers.hpp"
TYPES = "inc/ads7952_types.hpp"
UMBRELLA = "inc/ads7952.hpp"
IMPL = "src/ads7952.ipp"


def read(rel: str) -> str:
    return (ROOT / rel).read_text()


def need(pattern: str, text: str, what: str, flags: int = re.S) -> re.Match:
    m = re.search(pattern, text, flags)
    if not m:
        raise SystemExit(f"figure source drift: {what} not found (pattern {pattern!r})")
    return m


def constants(rel: str) -> dict[str, int]:
    """``inline constexpr`` integer constants of a header given as literals or shifts, by name."""
    out: dict[str, int] = {}
    for name, expr in re.findall(r"inline constexpr (?:u?int\d+_t|unsigned|std::size_t) (k\w+) =\s*([^;]+);",
                                 read(rel)):
        e = re.sub(r"(0x[0-9A-Fa-f']+|\d+)U", r"\1", expr.strip()).replace("'", "")
        for known in sorted(out, key=len, reverse=True):
            e = re.sub(rf"\b{known}\b", str(out[known]), e)
        if re.fullmatch(r"[0-9xA-Fa-f<>|&~() *+-]+", e):
            out[name] = int(eval(e))  # noqa: S307 - digits and operators only, checked above
    return out


def body(text: str, signature: str) -> str:
    """The body of the function whose definition contains ``signature``, braces matched."""
    start = need(re.escape(signature) + r"[^{;]*\{", text, signature).end()
    depth, i = 1, start
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[start : i - 1]
