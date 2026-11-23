/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

structure Metadata (Temp : Type) where
  next : Temp → Temp
  prev : Temp → Temp
  masks : MaskState Temp

def Metadata.valid (m : Metadata Temp) : Prop :=
  (∀ t, m.prev (m.next t) = t) ∧ (∀ t, m.next (m.prev t) = t)

def Metadata.sound (m : Metadata Temp) (s : State Temp) : Prop :=
  m.valid ∧ (∀ t, s (m.next t) = s t) ∧ SoundMasks m.masks s

def patch [DecidableEq Temp] (map : Temp → Value) (index : Temp) (value : Value) :
    Temp → Value :=
  fun t => if t = index then value else map t

def unlink [DecidableEq Temp] (m : Metadata Temp) (dst : Temp) : Metadata Temp :=
  let p := m.prev dst
  let n := m.next dst
  ⟨patch (patch m.next p n) dst dst, patch (patch m.prev n p) dst dst,
   setMasks m.masks dst Masks.unknown⟩

def insert [DecidableEq Temp] (m : Metadata Temp) (dst src : Temp) : Metadata Temp :=
  let next := m.next src
  ⟨patch (patch m.next src dst) dst next, patch (patch m.prev next dst) dst src,
   setMasks m.masks dst (m.masks src)⟩

def initialized (constants : Temp → Option Word) : Metadata Temp :=
  ⟨id, id, fun t => (constants t).elim Masks.unknown Masks.constant⟩

theorem initializedSound (s : State Temp) (constants : Temp → Option Word)
    (correct : ∀ t v, constants t = some v → s t = v) : (initialized constants).sound s := by
  refine ⟨⟨fun _ => rfl, fun _ => rfl⟩, fun _ => rfl, ?_⟩
  intro t
  cases h : constants t with
  | none => simpa [initialized, h] using unknownSound (s t)
  | some v => simpa [initialized, h, correct t v h] using constantSound v

theorem unlinkNext [DecidableEq Temp] (m : Metadata Temp) (dst : Temp) :
    (unlink m dst).next dst = dst := by
  simp [unlink, patch]

theorem unlinkPrev [DecidableEq Temp] (m : Metadata Temp) (dst : Temp) :
    (unlink m dst).prev dst = dst := by
  simp [unlink, patch]

theorem prevNotSingleton (m : Metadata Temp) (dst : Temp) (valid : m.valid)
    (next : m.next dst ≠ dst) : m.prev dst ≠ dst := by
  intro same
  exact next (by simpa [same] using valid.2 dst)

theorem unlinkValid [DecidableEq Temp] (m : Metadata Temp) (dst : Temp)
    (valid : m.valid) : (unlink m dst).valid := by
  by_cases alone : m.next dst = dst
  · have prev : m.prev dst = dst := by simpa [alone] using valid.1 dst
    have nextSame : (unlink m dst).next = m.next := by
      funext t
      by_cases alias : t = dst <;> simp [unlink, patch, alone, prev, alias]
    have prevSame : (unlink m dst).prev = m.prev := by
      funext t
      by_cases alias : t = dst <;> simp [unlink, patch, alone, prev, alias]
    exact ⟨fun t => by rw [nextSame, prevSame]; exact valid.1 t,
           fun t => by rw [nextSame, prevSame]; exact valid.2 t⟩
  · have before := prevNotSingleton m dst valid alone
    refine ⟨?_, ?_⟩
    · intro t
      by_cases removed : t = dst
      · subst t
        rw [unlinkNext, unlinkPrev]
      · by_cases predecessor : t = m.prev dst
        · subst t
          simp [unlink, patch, before, alone]
        · have notRemoved : m.next t ≠ dst := by
            intro eq
            exact predecessor (by simpa [eq] using (valid.1 t).symm)
          have notSuccessor : m.next t ≠ m.next dst := by
            intro eq
            exact removed ((valid.1 t).symm.trans ((congrArg m.prev eq).trans (valid.1 dst)))
          simp [unlink, patch, removed, predecessor, notRemoved, notSuccessor, valid.1]
    · intro t
      by_cases removed : t = dst
      · subst t
        rw [unlinkPrev, unlinkNext]
      · by_cases successor : t = m.next dst
        · subst t
          simp [unlink, patch, before, alone]
        · have notRemoved : m.prev t ≠ dst := by
            intro eq
            exact successor (by simpa [eq] using (valid.2 t).symm)
          have notPredecessor : m.prev t ≠ m.prev dst := by
            intro eq
            exact removed ((valid.2 t).symm.trans ((congrArg m.next eq).trans (valid.2 dst)))
          simp [unlink, patch, removed, successor, notRemoved, notPredecessor, valid.2]

theorem unlinkSound [DecidableEq Temp] (m : Metadata Temp) (s : State Temp)
    (dst : Temp) (value : Word) (facts : m.sound s) :
    (unlink m dst).sound (write s dst value) := by
  refine ⟨unlinkValid m dst facts.1, ?_, ?_⟩
  · intro t
    by_cases removed : t = dst
    · subst t
      rw [unlinkNext]
    · by_cases predecessor : t = m.prev dst
      · have notAlone : m.next dst ≠ dst := by
          intro same
          exact removed (predecessor.trans (by simpa [same] using facts.1.1 dst))
        have transitive := (facts.2.1 dst).trans
          ((congrArg s (facts.1.2 dst)).symm.trans (facts.2.1 (m.prev dst)))
        have before := prevNotSingleton m dst facts.1 notAlone
        simp [unlink, patch, removed, predecessor, before, notAlone, write, transitive]
      · have notRemoved : m.next t ≠ dst := by
          intro eq
          exact predecessor (by simpa [eq] using (facts.1.1 t).symm)
        simp [unlink, patch, removed, predecessor, notRemoved, write, facts.2.1]
  · intro t
    by_cases alias : t = dst
    · subst t
      simpa [unlink, setMasks, write] using unknownSound value
    · simpa [unlink, setMasks, write, alias] using facts.2.2 t

theorem insertValid [DecidableEq Temp] (m : Metadata Temp) (dst src : Temp)
    (valid : m.valid) (isolated : m.next dst = dst) (distinct : dst ≠ src) :
    (insert m dst src).valid := by
  have isolatedPrev : m.prev dst = dst := by simpa [isolated] using valid.1 dst
  have nextNotDst : m.next src ≠ dst := by
    intro eq
    exact distinct (isolatedPrev.symm.trans ((congrArg m.prev eq).symm.trans (valid.1 src)))
  refine ⟨?_, ?_⟩
  · intro t
    by_cases destination : t = dst
    · subst t
      simp [insert, patch, nextNotDst]
    · by_cases source : t = src
      · subst t
        simp [insert, patch, Ne.symm distinct]
      · have notDst : m.next t ≠ dst := by
          intro eq
          exact destination ((valid.1 t).symm.trans ((congrArg m.prev eq).trans isolatedPrev))
        have other : m.next t ≠ m.next src := by
          intro eq
          exact source ((valid.1 t).symm.trans ((congrArg m.prev eq).trans (valid.1 src)))
        simp [insert, patch, destination, source, notDst, other, valid.1]
  · intro t
    by_cases destination : t = dst
    · subst t
      simp [insert, patch, Ne.symm distinct]
    · by_cases successor : t = m.next src
      · subst t
        simp [insert, patch, nextNotDst]
      · have notDst : m.prev t ≠ dst := by
          intro eq
          exact destination ((valid.2 t).symm.trans ((congrArg m.next eq).trans isolated))
        have other : m.prev t ≠ src := by
          intro eq
          exact successor ((valid.2 t).symm.trans (congrArg m.next eq))
        simp [insert, patch, destination, successor, notDst, other, valid.2]

theorem insertSound [DecidableEq Temp] (m : Metadata Temp) (s : State Temp)
    (dst src : Temp) (facts : m.sound s) (isolated : m.next dst = dst)
    (distinct : dst ≠ src) (equal : s dst = s src) :
    (insert m dst src).sound s := by
  refine ⟨insertValid m dst src facts.1 isolated distinct, ?_, ?_⟩
  · intro t
    by_cases destination : t = dst
    · subst t
      simp [insert, patch, facts.2.1, equal]
    · by_cases source : t = src <;>
        simp [insert, patch, destination, source, Ne.symm distinct, facts.2.1, equal]
  · intro t
    by_cases destination : t = dst
    · subst t
      simpa [insert, setMasks, equal] using facts.2.2 src
    · simpa [insert, setMasks, destination] using facts.2.2 t

def follow (m : Metadata Temp) (start : Temp) : Nat → Temp
  | 0 => start
  | n + 1 => m.next (follow m start n)

theorem alongCopies (m : Metadata Temp) (s : State Temp) (facts : m.sound s) (start : Temp)
    (n : Nat) : s (follow m start n) = s start := by
  induction n with
  | zero => rfl
  | succ count ih => exact (facts.2.1 (follow m start count)).trans ih

/-- Returned true for self or after visiting the target during the C scan.
    The false path always distinguishes the two pointer arguments. -/
inductive CopyAnswer (m : Metadata Temp) (src dst : Temp) : Bool → Prop where
  | found (n : Nat) (reached : follow m src n = dst) : CopyAnswer m src dst true
  | rejected (distinct : src ≠ dst) : CopyAnswer m src dst false

theorem scannedCopies (m : Metadata Temp) (s : State Temp) (facts : m.sound s)
    (dst src : Temp) (answer : CopyAnswer m src dst true) : s dst = s src := by
  obtain ⟨n, reached⟩ := answer
  simpa only [reached] using alongCopies m s facts src n

end IA64Optimizer
