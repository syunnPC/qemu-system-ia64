# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests of wrapped instruction-format columns in pinned SDM extraction."""

import unittest
from pathlib import Path

from sdm import read_catalog, native_format_rows, replacement_formats


class NativeFormatTests(unittest.TestCase):
    def test_wrapped_load_form_keeps_both_adjacent_rows(self):
        rows = """
                 (qp) ld16.ldhint r1, ar.csd = [r3]             sixteen_byte_form                  M2
                 (qp) ld16.acq.ldhint r1, ar.csd = [r3]        sixteen_byte_form, acquire_form,
                                                              no_base_update_form                  M2
                 (qp) ld8.fill.ldhint r1 = [r3]                fill_form, no_base_update_form       M2
        """
        self.assertEqual(list(native_format_rows(rows, 151)),
                         [("ld16.ldhint", "M2"), ("ld16.acq.ldhint", "M2"),
                          ("ld8.fill.ldhint", "M2")])

    def test_unterminated_format_row_cannot_skip_to_next_form(self):
        rows = """
                 (qp) ld16.acq.ldhint r1, ar.csd = [r3]        sixteen_byte_form, acquire_form,
                 (qp) ld8.fill.ldhint r1 = [r3]                fill_form, no_base_update_form       M2
        """
        with self.assertRaisesRegex(ValueError, "incomplete native Format row on page 3:151"):
            list(native_format_rows(rows, 151))


class NativeUpdateTests(unittest.TestCase):
    def test_second_blank_brl_format_is_not_a_format_row(self):
        page = """V3-B brl — Branch Long

Format:          (qp) brl.btype.bwh.ph.dh target64              X3
                 (qp) brl.btype.bwh.ph.dh b1 = target64         X4
                      brl.ph.dh target64                      pseudo-op
Format:

Description: A branch condition is evaluated.
Intel® Itanium® Architecture Software Developer’s Manual Specification Update 141
"""
        self.assertEqual(replacement_formats(page, "B"),
                         (141, [("brl.btype.bwh.ph.dh", "X3"),
                                ("brl.btype.bwh.ph.dh", "X4"), ("brl.ph.dh", "pseudo-op")]))

    def test_effective_catalog_keeps_corrected_and_historical_encodings(self):
        snapshot = Path(__file__).with_name("sdm-index.tsv")
        effective = read_catalog(snapshot)
        base = read_catalog(snapshot, include_update=False)
        corrected = {
            "sdm-form:ld:ldsz.ldtype.ldhintx:M1:1",
            "sdm-form:ld:ldsz.ldtype.ldhint:M2:2",
            "sdm-form:st:stsz.sttype.sthintx:M4:1",
            "sdm-form:lfetch:lfetch.fault.lfhint:M13:2",
            "sdm-form:lfetch:lfetch.lftype.lfhint:M14:3",
            "sdm-form:lfetch:lfetch.lftype.lfhint:M15:4",
            "sdm-form:hint:hint.m:M49:4",
            "sdm-form:mov-dahr:mov:M50:1",
            "sdm-form:lfetch:lfetch.lfhint:M51:1",
            "sdm-form:lfetch:lfetch.count.lfhint:M52:8",
        }
        superseded = {
            "sdm-form:ld:ldsz.ldtype.ldhint:M2:1",
            "sdm-form:st:stsz.sttype.sthint:M6:1",
            "sdm-form:lfetch:lfetch.lftype.lfhint:M18:1",
        }
        self.assertLessEqual(corrected, effective.keys())
        self.assertLessEqual(superseded, base.keys())
        self.assertTrue(superseded.isdisjoint(effective))
        self.assertIn("sdm-form:hint:hint.m:M48:4", effective)


if __name__ == "__main__":
    unittest.main()
