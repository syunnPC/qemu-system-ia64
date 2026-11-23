/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace IA64Optimizer

local notation "Word" => BitVec 64

theorem emittedKill_eq [DecidableEq Temp] (m : Metadata Temp) (dst : Temp) :
    emittedKill m dst = unlink m dst := rfl

theorem patchCommutes [DecidableEq Temp] (table : Temp → Value) (a b : Temp) (x y : Value)
    (distinct : a ≠ b) :
    patch (patch table a x) b y = patch (patch table b y) a x := by
  funext t
  by_cases left : t = a
  · subst t
    simp [patch, distinct]
  · by_cases right : t = b <;> simp [patch, left, right, Ne.symm distinct]

theorem emittedLink_eq [DecidableEq Temp] (m : Metadata Temp) (dst src : Temp)
    (valid : m.valid) (isolated : m.next dst = dst) (different : dst ≠ src) :
    emittedLink m dst src = insert m dst src := by
  have isolatedPrev : m.prev dst = dst := by simpa [isolated] using valid.1 dst
  have nextNotDst : m.next src ≠ dst := by
    intro same
    exact different (isolatedPrev.symm.trans ((congrArg m.prev same).symm.trans (valid.1 src)))
  unfold emittedLink insert
  dsimp only
  rw [patchCommutes m.next dst src _ _ different,
      patchCommutes m.prev dst (m.next src) _ _ (Ne.symm nextNotDst)]

def scanTail [DecidableEq Temp] (m : Metadata Temp) (start target : Temp) :
    Temp → Nat → Option Bool
  | _, 0 => none
  | cursor, fuel + 1 =>
    if cursor = start then some false
    else if cursor = target then some true
    else scanTail m start target (m.next cursor) fuel

def emittedScan [DecidableEq Temp] (m : Metadata Temp) (start target : Temp) (fuel : Nat) :
    Option Bool :=
  if start = target then some true
  else if m.next start = start ∨ m.next target = target then some false
  else scanTail m start target (m.next start) fuel

theorem followPush (m : Metadata Temp) (cursor : Temp) (n : Nat) :
    follow m (m.next cursor) n = follow m cursor (n + 1) := by
  induction n with
  | zero => rfl
  | succ size ih => exact congrArg m.next ih

theorem scanReached [DecidableEq Temp] (m : Metadata Temp) (start target cursor : Temp)
    (fuel : Nat) (found : scanTail m start target cursor fuel = some true) :
    ∃ hops, follow m cursor hops = target := by
  induction fuel generalizing cursor with
  | zero => simp [scanTail] at found
  | succ size ih =>
    by_cases returned : cursor = start
    · simp [scanTail, returned] at found
    · by_cases hit : cursor = target
      · exact ⟨0, hit⟩
      · obtain ⟨hops, reached⟩ := ih (m.next cursor) (by simpa [scanTail, returned, hit] using found)
        exact ⟨hops + 1, (followPush m cursor hops).symm.trans reached⟩

theorem emittedScanSound [DecidableEq Temp] (m : Metadata Temp) (start target : Temp)
    (fuel : Nat) (answer : Bool) (reported : emittedScan m start target fuel = some answer) :
    CopyAnswer m start target answer := by
  by_cases same : start = target
  · have yes : answer = true := by simpa [emittedScan, same] using reported.symm
    subst answer
    exact .found 0 same
  · cases answer
    · exact .rejected same
    · by_cases singleton : m.next start = start ∨ m.next target = target
      · simp [emittedScan, same, singleton] at reported
      · obtain ⟨hops, reached⟩ := scanReached m start target (m.next start) fuel
          (by simpa [emittedScan, same, singleton] using reported)
        exact .found (hops + 1) ((followPush m start hops).symm.trans reached)

def pickCopy (kind : Temp → Nat) (a b : Temp) : Temp :=
  if kind a < kind b then b else a

theorem copyPreference [DecidableEq Temp] (m : Metadata Temp) (s : State Temp)
    (sound : m.sound s) (kind : Temp → Nat) (src candidate : Temp)
    (initial : s candidate = s src) (visits : List Nat) :
    s (visits.foldl (fun current n => pickCopy kind current (follow m src n)) candidate) = s src := by
  induction visits generalizing candidate with
  | nil => exact initial
  | cons n visits ih =>
    apply ih
    by_cases higher : kind candidate < kind (follow m src n)
    · simpa [pickCopy, higher] using alongCopies m s sound src n
    · simpa [pickCopy, higher] using initial

def emittedMove [DecidableEq Temp] (m : Metadata Temp) (dst src : Temp)
    (erase : Bool) : Metadata Temp × Operation Temp :=
  if erase then (m, .nop)
  else (emittedLink (emittedKill m dst) dst src, .replace .first dst src src)

theorem emittedMoveSound [DecidableEq Temp] (m : Metadata Temp) (s : State Temp)
    (dst src : Temp) (fuel : Nat) (erase : Bool)
    (sound : m.sound s) (reported : emittedScan m dst src fuel = some erase) :
    (emittedMove m dst src erase).1.sound ((emittedMove m dst src erase).2.run s).1 ∧
      (emittedMove m dst src erase).2.run s = (write s dst (s src), []) := by
  have scan := emittedScanSound m dst src fuel erase reported
  cases scan with
  | found n reached =>
    have copy := scannedCopies m s sound src dst (.found n reached)
    exact ⟨sound, by rw [emittedMove, if_pos rfl, Prod.snd, Operation.run, copy, writeSame]⟩
  | rejected distinct =>
    have killed := unlinkSound m s dst (s src) sound
    have isolated := unlinkNext m dst
    have merged := emittedLink_eq (unlink m dst) dst src killed.1 isolated distinct
    have equal : write s dst (s src) dst = write s dst (s src) src := by
      simp [write, Ne.symm distinct]
    refine ⟨?_, rfl⟩
    simp only [emittedMove, Bool.false_eq_true, if_false, Operation.run, emittedKill_eq, merged]
    exact insertSound _ _ _ _ killed isolated distinct equal

end IA64Optimizer
