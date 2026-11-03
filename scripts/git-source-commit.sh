#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Do not pick up a parent repository when building a source archive.
if [ -e "$1/.git" ]; then
    git -C "$1" rev-parse --verify HEAD 2>/dev/null || :
fi
