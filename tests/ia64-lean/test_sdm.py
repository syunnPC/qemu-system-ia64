# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests of wrapped instruction-format columns in pinned SDM extraction."""

import unittest

from sdm import native_format_rows


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


if __name__ == "__main__":
    unittest.main()
