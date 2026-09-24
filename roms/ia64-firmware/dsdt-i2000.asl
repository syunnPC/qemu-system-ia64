// SPDX-License-Identifier: GPL-2.0-or-later

// Compile with iasl -oi to encode numeric _PRT zeros as ByteConst objects.

DefinitionBlock ("", "DSDT", 2, "QEMU  ", "I2K4DSDT", 0x00000001)
{
    Name (_S5, Package (0x04)
    {
        0x04,
        0x04,
        Zero,
        Zero
    })

    Scope (_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 0, 0, 0, 1)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0, 0x000001CD, 0, 0x000001CE,
                    , , , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x000001D2, 0x000003AF, 0, 0x000001DE,
                    , , , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x000003E0, 0x00003FFF, 0, 0x00003C20,
                    , , , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0x90000000, 0x9FFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package ()
            {
                Package () { 0x0004FFFF, 0, 0, 16 },
                Package () { 0x0005FFFF, 0, 0, 16 },
                Package () { 0x0003FFFF, 3, 0, 19 }
            })

            Device (IFB0)
            {
                Name (_ADR, 0x00030000)

                Device (COM1)
                {
                    Name (_HID, EisaId ("PNP0501"))
                    Name (_UID, Zero)
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03F8, 0x03F8, 1, 8)
                        IRQ (Edge, ActiveHigh, Exclusive) {4}
                    })
                }

                Device (LPT1)
                {
                    Name (_HID, EisaId ("PNP0401"))
                    Name (_CID, EisaId ("PNP0400"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0378, 0x0378, 1, 3)
                        IO (Decode16, 0x0778, 0x0778, 1, 3)
                        IRQ (Level, ActiveHigh, Exclusive) {7}
                    })
                }

                Device (PS2K)
                {
                    Name (_HID, EisaId ("PNP0303"))
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
                    Name (_CRS, ResourceTemplate ()
                    {
                        IRQNoFlags () {12}
                    })
                }
            }
        }

        Device (PCI1)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_SEG, Zero)
            Name (_BBN, One)
            Name (_UID, One)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 1, 1, 0, 1)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x00004000, 0x00007FFF, 0, 0x00004000,
                    , , , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0xA0000000, 0xAFFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package ()
            {
                Package () { 0x0000FFFF, 0, 0, 20 }
            })
        }

        Device (PCI2)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_SEG, Zero)
            Name (_BBN, 0x02)
            Name (_UID, 0x02)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 2, 2, 0, 1)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x00008000, 0x0000BFFF, 0, 0x00004000,
                    , , , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0xB0000000, 0xBFFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package () {})
        }

        Device (PCI3)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_SEG, Zero)
            Name (_BBN, 0x03)
            Name (_UID, 0x03)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 3, 3, 0, 1)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x0000C000, 0x0000FFFF, 0, 0x00004000,
                    , , , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x000001CE, 0x000001D1, 0, 0x00000004,
                    , , , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x000003B0, 0x000003DF, 0, 0x00000030,
                    , , , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0x000A0000, 0x000FFFFF, 0, 0x00060000)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0xE0000000, 0xEFFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package ()
            {
                Package () { 0x0000FFFF, 0, 0, 28 }
            })
        }
    }
}
