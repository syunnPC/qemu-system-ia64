# SPDX-License-Identifier: GPL-2.0-or-later
"""Read the selected C function bodies for the memory checks."""

import re


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


def compact(text):
    return re.sub(r"\s+|/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def body(source, name):
    match = re.search(rf"\b{name}\s*\([^;{{}}]*\)\s*{{(.*?)^}}", source,
                      flags=re.MULTILINE | re.DOTALL)
    require(match is not None, f"missing function body {name}")
    return compact(match[1])
