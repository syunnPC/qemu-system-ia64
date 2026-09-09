.. SPDX-License-Identifier: GPL-2.0-or-later

GPU emulation source notice
===========================

The following sources were used as technical references:

* `envytools <https://github.com/envytools/envytools>`__ for NVIDIA NV15
  registers, object methods, PFIFO, PTIMER, and DMA behavior.  The NV10/NV15
  tile address mapping and status fields follow ``nvhw/tile.c``,
  ``hwtest/nv10_tile.cc`` and ``rnndb/memory/nv10_pfb.xml``.
* `Linux kernel <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git>`__
  for ATI Rage128/R100 and NVIDIA NV10/NV15 interfaces.  The ATI references
  include
  `include/video/aty128.h <https://github.com/torvalds/linux/blob/master/include/video/aty128.h>`__,
  `include/video/radeon.h <https://github.com/torvalds/linux/blob/master/include/video/radeon.h>`__,
  `drivers/gpu/drm/radeon/radeon_reg.h <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_reg.h>`__
  for register and command-packet definitions,
  `radeon_combios.c <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_combios.c>`__
  for legacy BIOS clock-table layout,
  `radeon_legacy_encoders.c <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_legacy_encoders.c>`__
  for primary-DAC load detection, and
  `radeon_legacy_crtc.c <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/radeon_legacy_crtc.c>`__
  for legacy tiled framebuffer panning.
* `X.Org xf86-video-nv <https://gitlab.freedesktop.org/xorg/driver/xf86-video-nv>`__
  for NVIDIA NV4/NV15 2D, video, and cursor programming.
* `RPCS3 <https://github.com/RPCS3/rpcs3>`__ for NVIDIA NV0039
  memory-to-memory transfer behavior.
* X.Org xf86-video-r128 and DirectFB for ATI Rage128 programming interfaces,
  as detailed below.
* `X.Org xf86-video-ati <https://gitlab.freedesktop.org/xorg/driver/xf86-video-ati>`__
  for ATI Radeon R100 command sequences.
* `Mesa <https://gitlab.freedesktop.org/mesa/mesa>`__ for ATI Radeon R100
  rendering and texture behavior.

ATI 2D and display references
-----------------------------

The references below describe guest programming interfaces.  The listed
approximations are emulator policies and do not establish exact hardware
behavior.

.. list-table:: Implementation and reference coverage
   :header-rows: 1
   :widths: 25 35 40

   * - Implementation
     - Public reference
     - Supported behavior and limits
   * - ``ati_2d_bresenham()`` in ``hw/display/ati_2d.c``
     - X.Org's ``R128SubsequentSolidBresenhamLine()``,
       ``R128SetupForDashedLine()`` and
       ``R128SubsequentDashedBresenhamLine()`` [r128-xorg]_; DirectFB's
       ``ati128DrawLine()`` [r128-directfb]_.
     - Straight and dashed lines, octant selection and brush phase.  Error
       terms use signed software arithmetic.  Setup registers retain their
       written values; hardware accumulator wrapping, ``DST_BRES_SIGN``
       tie-breaking and post-command register values are not modeled.
   * - ``ati_2d_trapezoid()`` in ``hw/display/ati_2d.c``
     - Register assignments in X.Org's ``R128SubsequentSolidFillTrap()``
       [r128-xorg]_.  This disabled driver hook supplies interface names,
       not a validated coverage rule.
     - Integer edge slopes with left-inclusive, right-exclusive spans.
       Fractional coordinates and lengths are truncated, and initial edge
       error terms are ignored.  Setup registers remain unchanged.
   * - ``ati_2d_stretch()`` and ``ati_2d_scale_blend()`` in
       ``hw/display/ati_2d.c``
     - DirectFB's ``ati128StretchBlit()`` [r128-directfb]_; the scale and
       replication flags in X.Org's ``src/r128_reg.h`` [r128-xorg]_.
     - Byte source offsets, format-dependent pitch units, 16.16 increments,
       and triggering through ``SCALE_DST_HEIGHT_WIDTH``.  Same-format RGB
       scaling uses replication or generic bilinear filtering, clamping at
       source edges and rounding once per channel.  Eight-bit pixels use
       replication.  Hardware filter precision and alpha blending are not
       modeled.
   * - ``ati_pll_write()`` and ``ati_crtc_frame_ns()`` in ``hw/display/ati.c``
     - X.Org's ``R128InitPLLRegisters()``, ``R128RestorePLLRegisters()`` and
       atomic-update helpers [r128-xorg]_; Linux's ``aty128_set_pll()`` and
       ``aty128_var_to_pll()`` [r128-linux]_, and the Radeon divider table in
       ``radeon_legacy_crtc.c`` [r100-linux]_.
     - Divider encodings, pixel-clock calculation and update completion.
       An atomic request applies the staged dividers immediately and clears
       the request bit.  Post-divider encoding 5 selects division by 16 on
       Radeon and is reserved on Rage128.  PLL settling and synchronization
       to vertical sync are not modeled.  Invalid timings use a 60 Hz fallback.
   * - Rage128 paths in ``ati_2d_tile_offset()`` and ``ati_scanout_read()``
     - The references above do not establish a physical tiled framebuffer
       address mapping.
     - Tiled 2D source and destination accesses are unimplemented and leave
       destination memory unchanged.  Tiled scanout returns zero pixel data
       (palette entry zero in indexed modes), including when panning.
       Linear surfaces remain available.
   * - Radeon paths in ``ati_2d_tile_offset()`` and ``ati_scanout_read()``
     - Mesa's ``radeon_span.c`` [r100-mesa]_ and Linux's
       ``radeon_crtc_do_set_base()`` [r100-linux]_.
     - Radeon macrotiles, 32-bit destination microtiles and tiled panning.
       These layouts are separate from the unsupported Rage128 layout.

Reference versions and licenses
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

License terms appear in the referenced file headers and DirectFB's
distributed license text.  Notices for adapted code are included in
``hw/display/ati.c``, ``hw/display/ati_regs.h`` and ``hw/display/ati_3d.c``.

.. [r128-xorg] `X.Org xf86-video-r128 6.10.2
   <https://xorg.freedesktop.org/archive/individual/driver/xf86-video-r128-6.10.2.tar.bz2>`__,
   ``src/r128_accel.c``, ``src/r128_driver.c`` and ``src/r128_reg.h``.
   Each file carries an MIT/X11-style permission notice allowing use,
   modification and redistribution with the copyright and permission notices.

.. [r128-directfb] DirectFB, commit
   ``5b474ffbe2e91b8363b3dd7d2c04cac191dbaa37``,
   `gfxdrivers/ati128/ati128.c
   <https://github.com/deniskropp/DirectFB/blob/5b474ffbe2e91b8363b3dd7d2c04cac191dbaa37/gfxdrivers/ati128/ati128.c>`__
   and ``ati128_set_clip()`` in
   `gfxdrivers/ati128/ati128_state.c
   <https://github.com/deniskropp/DirectFB/blob/5b474ffbe2e91b8363b3dd7d2c04cac191dbaa37/gfxdrivers/ati128/ati128_state.c>`__
   for packed-24 byte scissor coordinates.  Both headers permit LGPL version
   2 or later; the distributed
   `COPYING
   <https://github.com/deniskropp/DirectFB/blob/5b474ffbe2e91b8363b3dd7d2c04cac191dbaa37/COPYING>`__
   contains LGPL 2.1, whose section 3 permits conversion to GPL version 2.

.. [r128-linux] Linux v6.12,
   `drivers/video/fbdev/aty/aty128fb.c
   <https://github.com/torvalds/linux/blob/v6.12/drivers/video/fbdev/aty/aty128fb.c>`__
   (``GPL-2.0-only``) and
   `include/video/aty128.h
   <https://github.com/torvalds/linux/blob/v6.12/include/video/aty128.h>`__
   (``GPL-2.0``).  These are references for programming sequences and register
   definitions, respectively.

.. [r100-mesa] `Mesa 7.11.2
   <https://archive.mesa3d.org/older-versions/7.x/7.11.2/MesaLib-7.11.2.tar.bz2>`__,
   ``src/mesa/drivers/dri/radeon/radeon_span.c``.  Its MIT/X11-style notice,
   including the required Weather Channel attribution, is preserved with the
   adapted address equations in ``hw/display/ati_3d.c``.

.. [r100-linux] Linux v6.12,
   `drivers/gpu/drm/radeon/radeon_legacy_crtc.c
   <https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/radeon/radeon_legacy_crtc.c>`__.
   The file carries an MIT-style permission notice covering use, modification
   and redistribution with its notices.
