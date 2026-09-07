// SPDX-License-Identifier: GPL-2.0-or-later

DefinitionBlock ("", "SSDT", 2, "QEMU  ", "IA64SSDT", 0x00000001)
{
    // PCI0 is replaced with SBA0 for HP zx machines.
    // Omit generated external declarations from the runtime AML template.
    External (\_SB.PCI0, DeviceObj)

    Scope (\_SB)
    {
        // Firmware patches these AML BytePrefix payloads.
        Name (C0EN, 0x0F)
        Processor (CPU0, 0, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C0EN)
            }
        }

        Name (C1EN, 0x0F)
        Processor (CPU1, 1, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C1EN)
            }
        }

        Name (C2EN, 0x0F)
        Processor (CPU2, 2, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C2EN)
            }
        }

        Name (C3EN, 0x0F)
        Processor (CPU3, 3, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C3EN)
            }
        }

        Name (C4EN, 0x0F)
        Processor (CPU4, 4, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C4EN)
            }
        }

        Name (C5EN, 0x0F)
        Processor (CPU5, 5, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C5EN)
            }
        }

        Name (C6EN, 0x0F)
        Processor (CPU6, 6, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C6EN)
            }
        }

        Name (C7EN, 0x0F)
        Processor (CPU7, 7, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C7EN)
            }
        }

        Name (C8EN, 0x0F)
        Processor (CPU8, 8, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C8EN)
            }
        }

        Name (C9EN, 0x0F)
        Processor (CPU9, 9, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (C9EN)
            }
        }

        Name (E010, 0x0F)
        Processor (CP10, 10, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E010)
            }
        }

        Name (E011, 0x0F)
        Processor (CP11, 11, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E011)
            }
        }

        Name (E012, 0x0F)
        Processor (CP12, 12, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E012)
            }
        }

        Name (E013, 0x0F)
        Processor (CP13, 13, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E013)
            }
        }

        Name (E014, 0x0F)
        Processor (CP14, 14, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E014)
            }
        }

        Name (E015, 0x0F)
        Processor (CP15, 15, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E015)
            }
        }

        Name (E016, 0x0F)
        Processor (CP16, 16, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E016)
            }
        }

        Name (E017, 0x0F)
        Processor (CP17, 17, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E017)
            }
        }

        Name (E018, 0x0F)
        Processor (CP18, 18, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E018)
            }
        }

        Name (E019, 0x0F)
        Processor (CP19, 19, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E019)
            }
        }

        Name (E020, 0x0F)
        Processor (CP20, 20, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E020)
            }
        }

        Name (E021, 0x0F)
        Processor (CP21, 21, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E021)
            }
        }

        Name (E022, 0x0F)
        Processor (CP22, 22, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E022)
            }
        }

        Name (E023, 0x0F)
        Processor (CP23, 23, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E023)
            }
        }

        Name (E024, 0x0F)
        Processor (CP24, 24, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E024)
            }
        }

        Name (E025, 0x0F)
        Processor (CP25, 25, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E025)
            }
        }

        Name (E026, 0x0F)
        Processor (CP26, 26, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E026)
            }
        }

        Name (E027, 0x0F)
        Processor (CP27, 27, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E027)
            }
        }

        Name (E028, 0x0F)
        Processor (CP28, 28, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E028)
            }
        }

        Name (E029, 0x0F)
        Processor (CP29, 29, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E029)
            }
        }

        Name (E030, 0x0F)
        Processor (CP30, 30, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E030)
            }
        }

        Name (E031, 0x0F)
        Processor (CP31, 31, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E031)
            }
        }

        Name (E032, 0x0F)
        Processor (CP32, 32, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E032)
            }
        }

        Name (E033, 0x0F)
        Processor (CP33, 33, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E033)
            }
        }

        Name (E034, 0x0F)
        Processor (CP34, 34, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E034)
            }
        }

        Name (E035, 0x0F)
        Processor (CP35, 35, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E035)
            }
        }

        Name (E036, 0x0F)
        Processor (CP36, 36, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E036)
            }
        }

        Name (E037, 0x0F)
        Processor (CP37, 37, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E037)
            }
        }

        Name (E038, 0x0F)
        Processor (CP38, 38, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E038)
            }
        }

        Name (E039, 0x0F)
        Processor (CP39, 39, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E039)
            }
        }

        Name (E040, 0x0F)
        Processor (CP40, 40, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E040)
            }
        }

        Name (E041, 0x0F)
        Processor (CP41, 41, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E041)
            }
        }

        Name (E042, 0x0F)
        Processor (CP42, 42, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E042)
            }
        }

        Name (E043, 0x0F)
        Processor (CP43, 43, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E043)
            }
        }

        Name (E044, 0x0F)
        Processor (CP44, 44, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E044)
            }
        }

        Name (E045, 0x0F)
        Processor (CP45, 45, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E045)
            }
        }

        Name (E046, 0x0F)
        Processor (CP46, 46, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E046)
            }
        }

        Name (E047, 0x0F)
        Processor (CP47, 47, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E047)
            }
        }

        Name (E048, 0x0F)
        Processor (CP48, 48, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E048)
            }
        }

        Name (E049, 0x0F)
        Processor (CP49, 49, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E049)
            }
        }

        Name (E050, 0x0F)
        Processor (CP50, 50, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E050)
            }
        }

        Name (E051, 0x0F)
        Processor (CP51, 51, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E051)
            }
        }

        Name (E052, 0x0F)
        Processor (CP52, 52, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E052)
            }
        }

        Name (E053, 0x0F)
        Processor (CP53, 53, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E053)
            }
        }

        Name (E054, 0x0F)
        Processor (CP54, 54, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E054)
            }
        }

        Name (E055, 0x0F)
        Processor (CP55, 55, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E055)
            }
        }

        Name (E056, 0x0F)
        Processor (CP56, 56, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E056)
            }
        }

        Name (E057, 0x0F)
        Processor (CP57, 57, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E057)
            }
        }

        Name (E058, 0x0F)
        Processor (CP58, 58, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E058)
            }
        }

        Name (E059, 0x0F)
        Processor (CP59, 59, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E059)
            }
        }

        Name (E060, 0x0F)
        Processor (CP60, 60, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E060)
            }
        }

        Name (E061, 0x0F)
        Processor (CP61, 61, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E061)
            }
        }

        Name (E062, 0x0F)
        Processor (CP62, 62, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E062)
            }
        }

        Name (E063, 0x0F)
        Processor (CP63, 63, 0, 0)
        {
            Method (_STA, 0, NotSerialized)
            {
                Return (E063)
            }
        }
    }

    Scope (\_SB.PCI0)
    {
        // Firmware enables this only for the VPC platform.  The hardware
        // profiles describe their serial ports in their platform DSDTs.
        Name (U0EN, 0x0F)
        Device (UAR0)
        {
            Name (_HID, EisaId ("PNP0501"))
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized)
            {
                Return (U0EN)
            }
            Name (_CRS, ResourceTemplate ()
            {
                IO (Decode16, 0x03F8, 0x03F8, 1, 8)
                IRQNoFlags () {4}
            })
        }

        // Firmware patches this AML BytePrefix payload.
        Name (P2EN, 0x0F)

        Device (PS2K)
        {
            Name (_HID, EisaId ("PNP0303"))
            Method (_STA, 0, NotSerialized)
            {
                Return (P2EN)
            }
            Name (_CRS, ResourceTemplate ()
            {
                IO (Decode16, 0x0060, 0x0060, 1, 1)
                IO (Decode16, 0x0064, 0x0064, 1, 1)
                IRQNoFlags () {1}
            })
        }

        Device (PS2M)
        {
            Name (_HID, EisaId ("PNP0F13"))
            Method (_STA, 0, NotSerialized)
            {
                Return (P2EN)
            }
            Name (_CRS, ResourceTemplate ()
            {
                IRQNoFlags () {12}
            })
        }
    }

}
