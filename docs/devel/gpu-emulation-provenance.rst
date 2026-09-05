.. SPDX-License-Identifier: GPL-2.0-or-later

GPU emulation source notice
===========================

The following repositories were used as technical references:

* `envytools <https://github.com/envytools/envytools>`__ for NVIDIA NV15
  registers, object methods, PFIFO, PTIMER, and DMA behavior.
* `Linux kernel <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git>`__
  for ATI Rage128/R100 and NVIDIA NV10/NV15 interfaces.  The ATI references
  include
  `include/video/aty128.h <https://github.com/torvalds/linux/blob/master/include/video/aty128.h>`__,
  `include/video/radeon.h <https://github.com/torvalds/linux/blob/master/include/video/radeon.h>`__,
  `drivers/gpu/drm/radeon/radeon_reg.h <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_reg.h>`__
  for register and command-packet definitions,
  `radeon_combios.c <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_combios.c>`__
  for legacy BIOS clock-table layout, and
  `radeon_legacy_encoders.c <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_legacy_encoders.c>`__
  for primary-DAC load detection.
* `X.Org xf86-video-nv <https://gitlab.freedesktop.org/xorg/driver/xf86-video-nv>`__
  for NVIDIA NV4/NV15 2D, video, and cursor programming.
* `RPCS3 <https://github.com/RPCS3/rpcs3>`__ for NVIDIA NV0039
  memory-to-memory transfer behavior.
* `X.Org xf86-video-r128 <https://gitlab.freedesktop.org/xorg/driver/xf86-video-r128>`__
  for ATI Rage128 command sequences.
* `X.Org xf86-video-ati <https://gitlab.freedesktop.org/xorg/driver/xf86-video-ati>`__
  for ATI Radeon R100 command sequences.
* `Mesa <https://gitlab.freedesktop.org/mesa/mesa>`__ for ATI Radeon R100
  rendering and texture behavior.
