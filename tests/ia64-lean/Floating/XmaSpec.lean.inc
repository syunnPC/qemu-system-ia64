/- SPDX-License-Identifier: GPL-2.0-or-later -/

inductive XmaForm where
  | low | signedHigh | unsignedHigh | unsignedMultiply
  deriving DecidableEq

def xmaPayload (form : XmaForm) (a b addend : Word) : Word :=
  let product : BitVec 128 :=
    match form with
    | .low | .signedHigh => a.signExtend 128 * b.signExtend 128
    | .unsignedHigh | .unsignedMultiply => a.zeroExtend 128 * b.zeroExtend 128
  let sum := product + (if form = .unsignedMultiply then 0 else addend.zeroExtend 128)
  if form = .low then sum.extractLsb' 0 64 else sum.extractLsb' 64 64

def xmaRegister (form : XmaForm) (a b addend : Register) : Register :=
  let addend := if form = .unsignedMultiply then fixedZero else addend
  if a = natVal ∨ b = natVal ∨ addend = natVal then natVal
  else integerRegister (xmaPayload form (significand a) (significand b) (significand addend))
