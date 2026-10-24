# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# One static library per layer, each linked only against the layers below it
# (see docs/ROADMAP.md).  The layers are added here as they are written.

add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(oasp)
add_subdirectory(app)
