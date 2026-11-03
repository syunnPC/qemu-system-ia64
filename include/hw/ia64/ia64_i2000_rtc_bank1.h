/*
 * IA-64 i2000 test upper CMOS bank
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_I2000_RTC_BANK1_H
#define HW_IA64_I2000_RTC_BANK1_H

#include "hw/isa/isa.h"
#include "qom/object.h"

#define TYPE_IA64_I2000_RTC_BANK1 "ia64-i2000-rtc-bank1"
OBJECT_DECLARE_SIMPLE_TYPE(IA64I2000RTCBank1State, IA64_I2000_RTC_BANK1)

#define IA64_I2000_RTC_BANK1_PROP_IOBASE "iobase"
#define IA64_I2000_RTC_BANK1_SIZE 128U

#endif /* HW_IA64_I2000_RTC_BANK1_H */
