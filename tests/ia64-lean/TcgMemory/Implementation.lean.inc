/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory

def emittedEndian (descriptor : BitVec 32) (hostBig : Bool) : Bool :=
  hostBig != descriptor.getLsbD swapBit

def tcgLayout (base : BitVec 32) (hostBig guestBig : Bool) : Nat × Bool :=
  let descriptor := MemoryImplementation.tcgDescriptor base hostBig guestBig
  ((MemoryImplementation.memopSize descriptor).toNat, emittedEndian descriptor hostBig)

theorem loadLayout (w : Width) (hostBig guestBig : Bool) :
    tcgLayout (loadDescriptor w) hostBig guestBig =
      (w.bytes, if w = .w1 then hostBig else guestBig) := by
  cases w <;> cases guestBig <;> cases hostBig <;> decide

theorem storeLayout (w : Width) (rel hostBig guestBig : Bool) :
    tcgLayout (storeDescriptor w rel) hostBig guestBig =
      (w.bytes, if w = .w1 then hostBig else guestBig) := by
  cases w <;> cases rel <;> cases guestBig <;> cases hostBig <;> decide

theorem emittedLoadBytes (w : Width) (m : CellInput) :
    emittedLoadValue w (tcgLayout (loadDescriptor w) m.hostBig m.guestBig) m =
      unpack w.bytes m.guestBig m.bytes := by
  rw [loadLayout]
  cases m with
  | mk guestBig hostBig bytes preflight primitiveReturns hasMemBswap useSlowHelper =>
    cases w <;> cases guestBig <;> cases hostBig <;> cases hasMemBswap <;>
      simp [emittedLoadValue, loadNeedsSoftwareSwap, loadSwapWidth,
        bswapPayload, lowByte, unpack, significance, Width.bytes,
        List.range, List.range.loop, List.foldl] <;>
      simp (disch := decide) only [BitVec.ushiftRight_eq_extractLsb'_of_lt,
        BitVec.shiftLeft_eq_concat_of_lt] <;> bv_decide

theorem emittedStoreBytes (w : Width) (rel : Bool) (m : CellInput) (value : Word) :
    emittedStoreValue w (tcgLayout (storeDescriptor w rel) m.hostBig m.guestBig) m value =
      pack w.bytes m.guestBig value := by
  rw [storeLayout]
  cases m with
  | mk guestBig hostBig bytes preflight primitiveReturns hasMemBswap useSlowHelper =>
    cases w <;> cases guestBig <;> cases hostBig <;> cases hasMemBswap <;>
      simp [emittedStoreValue, storeNeedsSoftwareSwap, storeSwapWidth,
        bswapPayload, lowByte, pack, significance, Width.bytes, List.cons.injEq,
        List.range, List.range.loop, List.map, List.foldl] <;>
      simp (disch := decide) only [BitVec.ushiftRight_eq_extractLsb'_of_lt,
        BitVec.shiftLeft_eq_concat_of_lt] <;> bv_decide

theorem loadHelperOutcome (w : Width) (m : CellInput) :
    primitiveOutcome (loadHelperPlan w) m = ⟨m.primitiveReturns, m.primitiveReturns⟩ := by
  cases w <;> cases returned : m.primitiveReturns <;> cases slow : m.useSlowHelper <;>
    simp_all [primitiveOutcome, runHelper, loadHelperPlan]

theorem storeHelperOutcome (w : Width) (m : CellInput) :
    primitiveOutcome (storeHelperPlan w) m = ⟨m.primitiveReturns, m.primitiveReturns⟩ := by
  cases w <;> cases returned : m.primitiveReturns <;> cases slow : m.useSlowHelper <;>
    simp_all [primitiveOutcome, runHelper, storeHelperPlan]

def tcgMayWrite (s : Registers) (r : Reg) : Bool :=
  if r = 0 then false
  else if stackedBase ≤ r.val then decide (r.val - stackedBase < s.sof) else true

theorem legalTarget (s : Registers) (r : Reg) : tcgMayWrite s r = mayWrite s r := by
  unfold tcgMayWrite mayWrite stackedBase
  split <;> rename_i equal
  · simp [equal]
  · split <;> rename_i bound
    · have notSmall : ¬r.val < 32 := by omega
      simp [equal, notSmall]
    · have small : r.val < 32 := by omega
      simp [equal, small]

def tcgLoadUpdate (u : Update) (before after : Registers) (base : Reg) (address : Word) :
    Registers :=
  match u with
  | .none => after
  | .immediate d => setGR after base (loadImmediateAdd address d)
  | .register i =>
    setNaT (setGR after base (loadRegisterAdd address (sourceGR before i))) base
      (loadRegisterNaT (getNat before base) (getNat before i))

def tcgLoad (l : Load) (s : Registers) (m : CellInput) : Result :=
  if !predicate s l.qp then stop s .nullified
  else if !tcgMayWrite s l.target ||
      (l.update != .none && (!tcgMayWrite s l.base || l.base == l.target)) then
    stop s .illegalRegister
  else if getNat s l.base then stop s .addressNaT
  else if !m.preflight then stop s .preflight
  else
    let access := primitiveOutcome (loadHelperPlan l.width) m
    let layout := tcgLayout (loadDescriptor l.width) m.hostBig m.guestBig
    let address := sourceGR s l.base
    let effect := if access.touched then
      some (.read address ((List.range layout.1).map m.bytes) l.acquire) else none
    if !access.returned then ⟨s, effect, .primitiveAbort⟩
    else
      let value := emittedLoadValue l.width layout m
      let after := setNaT (setGR s l.target value) l.target false
      ⟨tcgLoadUpdate l.update s after l.base address, effect, .success⟩

def tcgStore (st : Store) (s : Registers) (m : CellInput) : Result :=
  if !predicate s st.qp then stop s .nullified
  else if st.update.isSome && !tcgMayWrite s st.base then stop s .illegalRegister
  else if getNat s st.base then stop s .addressNaT
  else if getNat s st.source then stop s .dataNaT
  else if !m.preflight then stop s .preflight
  else
    let access := primitiveOutcome (storeHelperPlan st.width) m
    let address := sourceGR s st.base
    let value := sourceGR s st.source
    let layout := tcgLayout (storeDescriptor st.width st.release) m.hostBig m.guestBig
    let effect := if access.touched then
      some (.write address (emittedStoreValue st.width layout m value) st.release) else none
    if !access.returned then ⟨s, effect, .primitiveAbort⟩
    else
      let after := match st.update with
        | none => s
        | some d => setGR s st.base (storeImmediateAdd address d)
      ⟨after, effect, .success⟩

theorem sourceRegister (s : Registers) (r : Reg) : sourceGR s r = getGR s r := by
  by_cases zero : r = 0 <;> simp [sourceGR, getGR, zero]

theorem loadLegalityBool (l : Load) (s : Registers) :
    (!tcgMayWrite s l.target ||
      (l.update != .none && (!tcgMayWrite s l.base || l.base == l.target))) =
        !legalLoad l s := by
  simp only [legalTarget, legalLoad, bne]
  generalize (mayWrite s l.target) = dst
  generalize (mayWrite s l.base) = base
  generalize (l.update == .none) = plain
  generalize (l.base == l.target) = overlap
  cases dst <;> cases base <;> cases plain <;> cases overlap <;> decide

theorem storeLegalityBool (st : Store) (s : Registers) :
    (st.update.isSome && !tcgMayWrite s st.base) = !legalStore st s := by
  simp only [legalTarget, legalStore]
  cases st.update <;> cases mayWrite s st.base <;> rfl

theorem loadValueState (l : Load) (s : Registers) (m : CellInput) :
    tcgLoad l s m = specLoad l s m := by
  simp only [tcgLoad, specLoad, loadLegalityBool, sourceRegister, emittedLoadBytes]
  have width := congrArg Prod.fst (loadLayout l.width m.hostBig m.guestBig)
  simp only [width]
  cases h : m.primitiveReturns <;> cases l.update <;>
    simp [loadHelperOutcome, h, stop, tcgLoadUpdate, loadImmediateAdd, loadRegisterAdd,
      loadRegisterNaT, sourceRegister]

theorem setNaTUnchanged (s : Registers) (r : Reg) (clear : s.nat r = false) :
    setNaT s r false = s := by
  cases s
  simp only [setNaT, Registers.mk.injEq, true_and, and_true]
  funext i
  by_cases equal : i = r <;> simp_all

theorem storeValueState (st : Store) (s : Registers) (m : CellInput) :
    tcgStore st s m = specStore st s m := by
  simp only [tcgStore, specStore, storeLegalityBool, sourceRegister, emittedStoreBytes]
  simp only [storeHelperOutcome]
  cases returns : m.primitiveReturns
  · simp [returns, stop]
  · cases update : st.update
    · simp [returns, update]
    · by_cases zero : st.base = 0
      · simp [legalStore, mayWrite, update, zero]
      · cases clear : s.nat st.base
        · have unchanged (value : Word) :
              setNaT (setGR s st.base value) st.base false = setGR s st.base value :=
            setNaTUnchanged _ _ clear
          simp [returns, update, storeImmediateAdd, unchanged]
        · simp [update, getNat, zero, clear]

-- A trap request before the primitive, or an abort from the primitive,
-- leaves the projected registers and the WB byte effect untouched.
theorem loadAbort (l : Load) (s : Registers) (m : CellInput)
    (failed : (tcgLoad l s m).exit ≠ .success) :
    (tcgLoad l s m).regs = s ∧ (tcgLoad l s m).effect = none := by
  rw [loadValueState] at *
  unfold specLoad
  unfold specLoad at failed
  repeat first | split <;> simp_all [stop] | rfl

theorem storeAbort (st : Store) (s : Registers) (m : CellInput)
    (failed : (tcgStore st s m).exit ≠ .success) :
    (tcgStore st s m).regs = s ∧ (tcgStore st s m).effect = none := by
  rw [storeValueState] at *
  unfold specStore
  unfold specStore at failed
  repeat first | split <;> simp_all [stop] | rfl

-- Ordinary load acquire is placed after the returning load and ordinary
-- store release before the attempted store by the audited C control flow.
-- Only source-generated TCG pair bits are used.
theorem acquirePairs :
    (acquireMask &&& loadLoadMask) = loadLoadMask ∧
    (acquireMask &&& loadStoreMask) = loadStoreMask := by decide

theorem releasePairs :
    (releaseMask &&& loadStoreMask) = loadStoreMask ∧
    (releaseMask &&& storeStoreMask) = storeStoreMask := by decide

theorem scalarWbProjection :
    (∀ (l : Load) (s : Registers) (m : CellInput), tcgLoad l s m = specLoad l s m) ∧
    (∀ (st : Store) (s : Registers) (m : CellInput), tcgStore st s m = specStore st s m) ∧
    ((acquireMask &&& loadLoadMask) = loadLoadMask ∧
      (acquireMask &&& loadStoreMask) = loadStoreMask) ∧
    ((releaseMask &&& loadStoreMask) = loadStoreMask ∧
      (releaseMask &&& storeStoreMask) = storeStoreMask) :=
  ⟨loadValueState, storeValueState, acquirePairs, releasePairs⟩

theorem eventProjection (cpu : Cpu) (s : Registers) (m : CellInput)
    (l : Load) (st : Store) :
    (tcgLoad l s m).effect.map (event cpu) = (specLoad l s m).effect.map (event cpu) ∧
    (tcgStore st s m).effect.map (event cpu) = (specStore st s m).effect.map (event cpu) :=
  ⟨congrArg (fun result => result.effect.map (event cpu)) (loadValueState l s m),
    congrArg (fun result => result.effect.map (event cpu)) (storeValueState st s m)⟩

-- The old loaded-to register is the base increment, while the returned
-- big-endian bytes become the new destination.
private def exampleRegs : Registers :=
  ⟨fun r => if r = 4 then 16 else 7, fun _ => false, fun _ => false, fun _ => false, 0⟩

private def exampleCell : CellInput :=
  ⟨true, false, fun i => if i = 0 then 0x12 else 0x34, true, true, false, true⟩

private def exampleLoad : Load :=
  ⟨0, 5, 4, .w2, true, .register 5⟩

private def exampleStore : Store :=
  ⟨0, 4, 4, .w2, true, some 2⟩

example :
    let result := tcgLoad exampleLoad exampleRegs exampleCell
    result.exit = .success ∧ result.regs.gr 5 = 0x1234 ∧ result.regs.gr 4 = 23 := by decide

example :
    let result := tcgStore exampleStore exampleRegs exampleCell
    result.exit = .success ∧ result.regs.gr 4 = 18 ∧
    (result.effect.map (event ())) = some ⟨some (), some (16, 2), .store, .release, 0x1000⟩ :=
  ⟨rfl, rfl, rfl⟩

example : (tcgLoad { exampleLoad with target := 0 } exampleRegs exampleCell).exit =
    .illegalRegister := by decide

example : (tcgStore { exampleStore with qp := 3 } exampleRegs exampleCell).exit =
    .nullified := by decide

example : (tcgStore exampleStore exampleRegs { exampleCell with primitiveReturns := false }).exit =
    .primitiveAbort := by decide

end TcgMemory
