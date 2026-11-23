/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Tcg

local notation "Word" => BitVec 64

structure Flow where
  pred : Predicate → Option Bool
  nat : Reg → Option Bool
  dirty : Reg → Bool
  checkedSof : Nat

def Flow.facts (flow : Flow) (i : Slot) : Facts :=
  ⟨flow.pred i.qp, flow.nat, flow.dirty, flow.checkedSof⟩

def Flow.Sound (s : State α) (flow : Flow) : Prop :=
  (∀ p b, flow.pred p = some b → SDM.qualified s p = b) ∧
  (∀ r b, flow.nat r = some b → SDM.nat s r = b) ∧
  (∀ r, flow.dirty r = true → s.dirty r = true) ∧
  flow.checkedSof ≤ s.sof

theorem Flow.soundAt (s : State α) (flow : Flow) (i : Slot) (h : Flow.Sound s flow) :
    IA64Tcg.Sound s i (flow.facts i) :=
  ⟨h.1 i.qp, h.2.1, h.2.2.1, h.2.2.2⟩

def lookupNatClear (s : State α) : Bool := (s.natWord 0 ||| s.natWord 1) == 0#64

def newFlow (natKey : Bool) : Flow :=
  ⟨fun p => if p = 0 then some true else none,
   fun r => if natKey || r == 0 then some false else none,
   fun _ => false, 0⟩

theorem lookupClear_words (s : State α) (flag : lookupNatClear s = true) :
    s.natWord 0 = 0#64 ∧ s.natWord 1 = 0#64 := by
  change ((s.natWord 0 ||| s.natWord 1) == 0#64) = true at flag
  constructor <;> bv_decide

theorem wordIndex_range (r : Reg) : wordIndex r = 0 ∨ wordIndex r = 1 := by
  have bound := r.isLt
  unfold wordIndex
  omega

theorem freshEntry_sound (s : State α) (key : Bool)
    (entry : key = true → lookupNatClear s = true) :
    Flow.Sound s (newFlow key) := by
  refine ⟨?_, ?_, ?_, Nat.zero_le _⟩
  · intro p b fact
    by_cases zero : p = 0
    · simp [newFlow, zero] at fact
      simp [SDM.qualified, zero, ← fact]
    · simp [newFlow, zero] at fact
  · intro r b fact
    by_cases zero : r = 0
    · simp [newFlow, zero] at fact
      subst b
      simp [SDM.nat, zero]
    · cases flag : key
      · simp [newFlow, zero, flag] at fact
      · have zeros := lookupClear_words s (entry flag)
        simp [newFlow, zero, flag] at fact
        subst b
        rcases wordIndex_range r with index | index <;>
          simp [SDM.nat, index, zeros.1, zeros.2]
  · intro _ bit
    cases bit

-- A nonzero bit can satisfy neither side of the clear-key Boolean test.
def guardedDirectEdge (key : Bool) (s : State α) : Bool :=
  if key then lookupNatClear s else !lookupNatClear s

theorem guardedEntry_sound (s : State α) (key : Bool)
    (passes : guardedDirectEdge key s = true) : Flow.Sound s (newFlow key) := by
  apply freshEntry_sound
  intro clearKey
  simpa [guardedDirectEdge, clearKey] using passes

def bitRead (word index : Word) : Bool := ((word >>> index) &&& 1#64) != 0

theorem bitIndex_lt (r : Reg) : bitIndex r < 64#64 := by
  change (bitIndex r).toNat < 64
  rw [bitIndex, BitVec.toNat_ofNat, Nat.mod_eq_of_lt]
  · exact Nat.mod_lt _ (by decide)
  · have bound := Nat.mod_lt r.val (by decide : 0 < 64)
    omega

theorem packedReadWrite (word index : Word) (bit : Bool)
    (bound : index < 64#64) :
    bitRead (IA64Integer.SDM.replaceNaT word index bit) index = bit := by
  simp only [bitRead, IA64Integer.SDM.replaceNaT]
  bv_decide

theorem packedReadOther (word dst queried : Word) (bit : Bool)
    (bound : dst < 64#64) (otherBound : queried < 64#64) (distinct : queried ≠ dst) :
    bitRead (IA64Integer.SDM.replaceNaT word dst bit) queried = bitRead word queried := by
  simp only [bitRead, IA64Integer.SDM.replaceNaT]
  bv_decide

theorem sameWordDistinctBit (dst queried : Reg)
    (sameWord : wordIndex queried = wordIndex dst) (distinct : queried ≠ dst) :
    bitIndex queried ≠ bitIndex dst := by
  intro sameBit
  apply distinct
  apply Fin.ext
  have sameIndex := congrArg BitVec.toNat sameBit
  have dstBound := Nat.mod_lt dst.val (by decide : 0 < 64)
  have queryBound := Nat.mod_lt queried.val (by decide : 0 < 64)
  have dstSmall : dst.val % 64 < 2^64 := by omega
  have querySmall : queried.val % 64 < 2^64 := by omega
  simp only [bitIndex, BitVec.toNat_ofNat, Nat.mod_eq_of_lt dstSmall,
             Nat.mod_eq_of_lt querySmall] at sameIndex
  unfold wordIndex at sameWord
  omega

theorem nat_commit_at (s : State α) (dst : Reg) (v : Word) (bit : Bool)
    (nonzero : dst ≠ 0) :
    SDM.nat (SDM.commit s dst v bit) dst = bit := by
  have notZero : (dst != 0) = true := bne_iff_ne.mpr nonzero
  simp only [SDM.nat, notZero, Bool.true_and, SDM.commit, update, if_pos rfl]
  exact packedReadWrite _ _ _ (bitIndex_lt _)

theorem nat_commit_other (s : State α) (dst query : Reg) (v : Word) (bit : Bool)
    (distinct : query ≠ dst) :
    SDM.nat (SDM.commit s dst v bit) query = SDM.nat s query := by
  by_cases sameWord : wordIndex query = wordIndex dst
  · simp only [SDM.nat, SDM.commit, update, if_pos sameWord]
    have change := packedReadOther (s.natWord (wordIndex dst)) (bitIndex dst) (bitIndex query)
      bit (bitIndex_lt _) (bitIndex_lt _) (sameWordDistinctBit _ _ sameWord distinct)
    unfold bitRead at change
    simp only [sameWord, change]
  · simp only [SDM.nat, SDM.commit, update, if_neg sameWord]

def mergeFact (before after : Option Bool) : Option Bool :=
  if before = after then before else none

def orFact (left right : Option Bool) : Option Bool :=
  if left = some true || right = some true then some true
  else if left = some false && right = some false then some false
  else none

def factSound (fact : Option Bool) (actual : Bool) : Prop :=
  ∀ b, fact = some b → actual = b

theorem mergeFactSound (before after : Option Bool) (old new taken : Bool)
    (left : factSound before old) (right : factSound after new) :
    factSound (mergeFact before after) (if taken then new else old) := by
  by_cases same : before = after
  · intro b fact
    simp only [mergeFact, same, if_true] at fact
    cases taken
    · exact left b (same.trans fact)
    · exact right b fact
  · simp [mergeFact, same, factSound]

theorem orFactSound (a b : Option Bool) (x y : Bool)
    (left : factSound a x) (right : factSound b y) :
    factSound (orFact a b) (x || y) := by
  cases a with
  | none => cases b <;> cases x <;> cases y <;> simp_all [orFact, factSound]
  | some a =>
    cases a <;> cases b <;> cases x <;> cases y <;> simp_all [orFact, factSound]

def resultFact (flow : Flow) (op : Alu) (i : Slot) : Option Bool :=
  orFact (if op.usesR2 then sourceKnownNat (flow.facts i) i.r2 else some false)
         (if op.usesR3 then sourceKnownNat (flow.facts i) i.r3 else some false)

def nextFlow (flow : Flow) (op : Alu) (i : Slot) : Flow :=
  let f := flow.facts i
  if f.predicate = some false then flow
  else
    let new := resultFact flow op i
    let selected := if f.predicate = some true then new else mergeFact (sourceKnownNat f i.dst) new
    let assertStacked := f.predicate = some true ∧ 32 ≤ i.dst.val
    { flow with
      nat := if i.dst = 0 then flow.nat else update flow.nat i.dst selected
      dirty := if assertStacked then update flow.dirty i.dst true else flow.dirty
      checkedSof := if assertStacked then max (i.dst.val - 32 + 1) flow.checkedSof
                    else flow.checkedSof }

theorem sourceKnownNatSound (s : State α) (flow : Flow) (i : Slot) (r : Reg)
    (sound : Flow.Sound s flow) :
    factSound (sourceKnownNat (flow.facts i) r) (SDM.nat s r) := by
  intro b fact
  have established := sourceNatInput_eq s i (flow.facts i) r (flow.soundAt s i sound)
  simp [sourceNatInput, fact] at established
  exact established.symm

theorem resultFactSound (s : State α) (flow : Flow) (i : Slot) (op : Alu)
    (sound : Flow.Sound s flow) :
    factSound (resultFact flow op i)
      (IA64Integer.SDM.sourceNaT op.usesR2 op.usesR3 (SDM.nat s i.r2) (SDM.nat s i.r3)) := by
  apply orFactSound
  · cases reads : op.usesR2
    · simp [reads, factSound]
    · simpa [reads] using sourceKnownNatSound s flow i i.r2 sound
  · cases reads : op.usesR3
    · simp [reads, factSound]
    · simpa [reads] using sourceKnownNatSound s flow i i.r3 sound

end IA64Tcg

namespace IA64Tcg

def nextState (s : State α) (i : Slot) (op : Alu) : State α :=
  if SDM.qualified s i.qp then SDM.operate s i op else s

def Reaches (outcome : Outcome α) (s : State α) : Prop :=
  outcome = .committed s ∨ outcome = .nullified s

theorem executeCondition (s : State α) (i : Slot) (selected : SDM.prepare s i = .execute) :
    SDM.qualified s i.qp = true ∧ SDM.legalTarget s i.dst = true := by
  unfold SDM.prepare at selected
  split at selected
  · contradiction
  · split at selected
    · contradiction
    · rename_i _ enabled
      split at selected
      · contradiction
      · split at selected
        · contradiction
        · split at selected
          · contradiction
          · rename_i allowed
            refine ⟨by simpa using enabled, ?_⟩
            cases legal : SDM.legalTarget s i.dst
            · simp [legal] at allowed
            · rfl

theorem nullifyCondition (s : State α) (i : Slot) (selected : SDM.prepare s i = .nullified) :
    SDM.qualified s i.qp = false := by
  unfold SDM.prepare at selected
  split at selected
  · contradiction
  · split at selected
    · rename_i _ disabled
      simpa using disabled
    · split at selected
      · contradiction
      · split at selected
        · contradiction
        · split at selected <;> contradiction

theorem reachedProjection (s t : State α) (i : Slot) (op : Alu) (o : Outcome α)
    (actual : o = sdStep s i op) (retired : Reaches o t) :
    t = nextState s i op ∧
      (SDM.qualified s i.qp = true → SDM.legalTarget s i.dst = true) := by
  rw [actual] at retired
  cases selected : SDM.prepare s i with
  | execute =>
    have conditions := executeCondition s i selected
    simp [Reaches, sdStep, finish, selected] at retired
    exact ⟨by simpa [nextState, conditions.1] using retired.symm, fun _ => conditions.2⟩
  | nullified =>
    have disabled := nullifyCondition s i selected
    simp [Reaches, sdStep, finish, selected] at retired
    exact ⟨by simpa [nextState, disabled] using retired.symm, by simp [disabled]⟩
  | fault _ => simp [Reaches, sdStep, finish, selected] at retired

theorem legalNonzero (s : State α) (r : Reg) (legal : SDM.legalTarget s r = true) :
    r ≠ 0 := by
  intro zero
  simp [SDM.legalTarget, zero] at legal

theorem nextStateNat (s : State α) (i : Slot) (op : Alu) (query : Reg)
    (legal : SDM.qualified s i.qp = true → SDM.legalTarget s i.dst = true) :
    SDM.nat (nextState s i op) query =
      if SDM.qualified s i.qp && query == i.dst then
        IA64Integer.SDM.sourceNaT op.usesR2 op.usesR3 (SDM.nat s i.r2) (SDM.nat s i.r3)
      else SDM.nat s query := by
  cases taken : SDM.qualified s i.qp
  · simp [nextState, taken]
  · by_cases alias : query = i.dst
    · subst query
      simp only [nextState, taken, if_true, Bool.true_and, beq_self_eq_true, SDM.operate]
      exact nat_commit_at _ _ _ _ (legalNonzero _ _ (legal taken))
    · simp [nextState, taken, alias, SDM.operate, nat_commit_other s i.dst query _ _ alias]

theorem nextStateKeepsDirty (s : State α) (i : Slot) (op : Alu) (query : Reg)
    (old : s.dirty query = true) : (nextState s i op).dirty query = true := by
  unfold nextState
  split
  · by_cases static : i.dst.val < 32
    · simp [SDM.operate, SDM.commit, static, old]
    · by_cases alias : query = i.dst <;> simp [SDM.operate, SDM.commit, static, update, alias, old]
  · exact old

theorem nextStateMarksDirty (s : State α) (i : Slot) (op : Alu)
    (taken : SDM.qualified s i.qp = true) (stacked : 32 ≤ i.dst.val) :
    (nextState s i op).dirty i.dst = true := by
  have notStatic : ¬ i.dst.val < 32 := by omega
  simp [nextState, taken, SDM.operate, SDM.commit, notStatic, update]

theorem stepPreservesFacts (s : State α) (i : Slot) (op : Alu) (flow : Flow)
    (invariant : Flow.Sound s flow)
    (legal : SDM.qualified s i.qp = true → SDM.legalTarget s i.dst = true) :
    Flow.Sound (nextState s i op) (nextFlow flow op i) := by
  by_cases off : flow.pred i.qp = some false
  · have taken := invariant.1 i.qp false off
    simpa [nextFlow, Flow.facts, off, nextState, taken] using invariant
  have effectPred : (nextState s i op).pr = s.pr := by
    unfold nextState
    split <;> rfl
  have effectSof : (nextState s i op).sof = s.sof := by
    unfold nextState
    split <;> rfl
  refine ⟨?_, ?_, ?_, ?_⟩
  · intro p b fact
    simp [nextFlow, Flow.facts, off] at fact
    unfold SDM.qualified
    rw [effectPred]
    exact invariant.1 p b fact
  · intro query bit fact
    rw [nextStateNat s i op query legal]
    by_cases alias : query = i.dst
    · subst query
      simp only [beq_self_eq_true, Bool.and_true]
      by_cases dstZero : i.dst = 0
      · have preserve : SDM.qualified s i.qp = false := by
          cases taken : SDM.qualified s i.qp
          · rfl
          · exact False.elim (legalNonzero s i.dst (legal taken) dstZero)
        simp [nextFlow, Flow.facts, off, dstZero] at fact
        have prior : flow.nat i.dst = some bit := by simpa [dstZero] using fact
        simp [preserve, invariant.2.1 i.dst bit prior]
      · by_cases on : flow.pred i.qp = some true
        · have taken := invariant.1 i.qp true on
          simp [nextFlow, Flow.facts, off, on, dstZero, update] at fact
          simpa [taken] using resultFactSound s flow i op invariant bit fact
        · simp [nextFlow, Flow.facts, off, on, dstZero, update] at fact
          exact mergeFactSound _ _ _ _ _ (sourceKnownNatSound s flow i i.dst invariant)
            (resultFactSound s flow i op invariant) bit fact
    · have prior : flow.nat query = some bit := by
        by_cases dstZero : i.dst = 0 <;>
          simpa [nextFlow, Flow.facts, off, dstZero, update, alias] using fact
      simp [alias, invariant.2.1 query bit prior]
  · intro query known
    by_cases asserted : flow.pred i.qp = some true ∧ 32 ≤ i.dst.val
    · by_cases alias : query = i.dst
      · subst query
        exact nextStateMarksDirty s i op (invariant.1 i.qp true asserted.1) asserted.2
      · have prior : flow.dirty query = true := by
          simpa [nextFlow, Flow.facts, off, asserted, update, alias] using known
        exact nextStateKeepsDirty s i op query (invariant.2.2.1 query prior)
    · have prior : flow.dirty query = true := by
        simpa [nextFlow, Flow.facts, off, asserted] using known
      exact nextStateKeepsDirty s i op query (invariant.2.2.1 query prior)
  · rw [effectSof]
    by_cases asserted : flow.pred i.qp = some true ∧ 32 ≤ i.dst.val
    · have valid := legal (invariant.1 i.qp true asserted.1)
      have notStatic : ¬ i.dst.val < 32 := by omega
      have fit : i.dst.val < 32 + s.sof := by
        simpa [SDM.legalTarget, legalNonzero s i.dst valid, notStatic] using valid
      simp [nextFlow, Flow.facts, off, asserted, Nat.max_le, invariant.2.2.2]
      omega
    · simpa [nextFlow, Flow.facts, off, asserted] using invariant.2.2.2

/-- Only retiring/nullified scalar instructions continue in the same abstract
    trace. A fault, an unknown opcode, a helper which changes these maps, or
    an unproved chain edge needs a new entry invariant. -/
inductive ScalarTrace {α : Type} : State α → Flow → State α → Flow → Prop where
  | nil (s : State α) (facts : Flow) : ScalarTrace s facts s facts
  | more {s t : State α} {initial facts : Flow} (i : Slot) (op : Alu)
      (tail : ScalarTrace s initial t facts)
      (legal : SDM.qualified t i.qp = true → SDM.legalTarget t i.dst = true) :
      ScalarTrace s initial (nextState t i op) (nextFlow facts op i)

theorem checkedStepExtends {s t u : State α} {initial facts : Flow} (i : Slot) (op : Alu)
    (tail : ScalarTrace s initial t facts) (o : Outcome α) (actual : o = sdStep t i op)
    (retired : Reaches o u) : ScalarTrace s initial u (nextFlow facts op i) := by
  obtain ⟨rfl, legal⟩ := reachedProjection t u i op o actual retired
  exact .more i op tail legal

theorem traceFacts {s t : State α} {initial facts : Flow}
    (trace : ScalarTrace s initial t facts) (sound : Flow.Sound s initial) :
    Flow.Sound t facts := by
  induction trace with
  | nil => exact sound
  | more i op _ legal ih => exact stepPreservesFacts _ i op _ (ih sound) legal

theorem lookupTraceFacts {start finish : State α} {final : Flow}
    (trace : ScalarTrace start (newFlow (lookupNatClear start)) finish final) :
    Flow.Sound finish final :=
  traceFacts trace (freshEntry_sound _ _ id)

end IA64Tcg
