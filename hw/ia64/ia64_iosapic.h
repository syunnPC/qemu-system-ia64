/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_IOSAPIC_H
#define HW_IA64_IOSAPIC_H

#include "hw/core/sysbus.h"
#include "hw/core/irq.h"

#define TYPE_IA64_IOSAPIC "ia64-iosapic"
OBJECT_DECLARE_SIMPLE_TYPE(IA64IOSapicState, IA64_IOSAPIC)

#define IA64_IOSAPIC_NUM_PINS 24
#define IA64_IOSAPIC_VERSION \
    (((IA64_IOSAPIC_NUM_PINS - 1) << 16) | 0x11)

#endif
