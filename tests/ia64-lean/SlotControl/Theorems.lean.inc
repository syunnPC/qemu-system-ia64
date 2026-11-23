/- SPDX-License-Identifier: GPL-2.0-or-later -/

def RISound (s : State) (known : Option Word) : Prop :=
  ∀ n, known = some n → SDM.ri s.psr = n

theorem riUnchanged (psr next : Word) (same : SDM.ri psr = next) :
    psr = SDM.withRI psr next := by
  simp only [SDM.ri, SDM.withRI] at *
  bv_decide

theorem toggledRI (psr prev next : Word) (same : SDM.ri psr = prev)
    (bound : next < 4) :
    psr ^^^ ((prev ^^^ next) <<< 41#64) = SDM.withRI psr next := by
  simp only [SDM.ri, SDM.withRI] at *
  bv_decide

theorem emittedRI (s : State) (known : Option Word) (next : Word)
    (sound : RISound s known) (bound : next < 4) :
    (emitRI known next).run s = ({s with psr := SDM.withRI s.psr next}, []) := by
  cases known with
  | none =>
    by_cases zero : next = 0#64
    · subst next
      simp [emitRI, Effect.run, Expr.eval, read, assign, SDM.withRI]
    · simp only [emitRI, if_neg zero, Effect.run, Expr.eval, read, assign, SDM.withRI,
                 List.nil_append]
  | some old =>
    have current := sound old rfl
    by_cases same : old = next
    · have equal := riUnchanged s.psr next (current.trans same)
      simp [emitRI, same, Effect.run, ← equal]
    · simp only [emitRI, if_neg same, Effect.run, Expr.eval, read, assign]
      rw [toggledRI s.psr old next current bound]

theorem emittedAdvance (s : State) (known : Option Word) (bundle : Word)
    (slot : Slot) (long : Bool) (sound : RISound s known) (current : s.ip = bundle) :
    (emitAdvance known bundle slot long).run s = (SDM.restart s bundle slot long, []) := by
  have a := emittedRI s known 0#64 sound (by decide)
  have b := emittedRI s known 1#64 sound (by decide)
  have c := emittedRI s known 2#64 sound (by decide)
  cases slot <;> cases long <;>
    simp [emitAdvance, Slot.number, a, b, c, SDM.restart, SDM.successor,
          Effect.run, Expr.eval, assign, ← current]

theorem emittedSuccess (s : State) (known : Option Bool) (record track : Bool) (bundle : Word)
    (sound : ∀ value, known = some value → SDM.collecting s.psr = value)
    (shared : record = false → SDM.collecting s.psr = true →
      s.lastSuccess = bundle &&& ~~~15#64) :
    (emitSuccess known record track bundle).run s =
      ({s with lastSuccess := SDM.success s bundle},
       if track && s.savedSuppression != 0 then [.clearSuppression] else []) := by
  have unrecorded (off : record = false) : SDM.success s bundle = s.lastSuccess := by
    unfold SDM.success
    split
    · exact (shared off (by assumption)).symm
    · rfl
  cases know : known with
  | none =>
    cases recording : record
    · cases track <;> by_cases empty : s.savedSuppression = 0#64 <;>
        simp [emitSuccess, know, recording, Effect.run, Expr.eval, read, unrecorded recording, empty] <;>
        cases s <;> simp_all
    · by_cases collecting : s.psr &&& 8192#64 = 0#64 <;> cases track <;>
        by_cases empty : s.savedSuppression = 0#64 <;>
        simp [emitSuccess, know, recording, Effect.run, Expr.eval, read, assign,
              SDM.success, SDM.collecting, collecting, empty] <;> cases s <;> simp_all
  | some bit =>
    have collecting := sound bit know
    cases recording : record
    · cases bit <;> cases track <;> by_cases empty : s.savedSuppression = 0#64 <;>
        simp [emitSuccess, know, recording, Effect.run, Expr.eval, read, unrecorded recording, empty] <;>
        cases s <;> simp_all
    · cases bit <;> cases track <;> by_cases empty : s.savedSuppression = 0#64 <;>
        simp [emitSuccess, know, recording, Effect.run, Expr.eval, read, assign,
              SDM.success, collecting, empty] <;> cases s <;> simp_all

theorem snapshotPrior (s : State) :
    emitSnapshot.run s = ({ s with savedPsr := s.psr }, []) := rfl

theorem emittedGroup (s : State) (stop : Bool) :
    (emitGroupAtExit stop).run s = ({ s with groupStart := stop }, []) := by
  cases stop <;> rfl

-- The checker is called on sequential completion whenever the address
-- implementation check or single stepping may produce a trap. A dynamic
-- PSR may also cause a conservative request that returns without a trap.
theorem emittedNative (current before : State) (saved dynamic ss virtual va pa : Bool)
    (bundle : Word) (slot : Slot) (long : Bool)
    (prior : (if saved then current.savedPsr else current.psr) = before.psr) :
    (emitNative current saved dynamic ss virtual va pa bundle slot long).run current =
      (current, if dynamic || ss || !(if virtual then va else pa) then
                  [.native (SDM.trapPayload before bundle slot long)] else []) := by
  cases virtual <;> cases slot <;> cases long <;>
    simp [emitNative, Slot.number, SDM.trapPayload, SDM.successor, SDM.completionSlot,
          prior, Effect.run]
  all_goals split <;> rfl

theorem nativeStaticSingleStep (s : State) (ss virtual : Bool)
    (bundle : Word) (slot : Slot) (long : Bool) (flag : ss = SDM.singleStep s.psr) :
    (emitNative s false false ss virtual true true bundle slot long).run s =
      (s, if SDM.singleStep s.psr then [.native (SDM.trapPayload s bundle slot long)] else []) := by
  rw [emittedNative s s false false ss virtual true true _ _ _ rfl, flag]
  cases virtual <;> simp

theorem emittedDebug (s : State) (dynamic db : Bool) (address : Address)
    (sound : dynamic = false → db = SDM.instructionDebug s.psr) :
    (emitDebug dynamic db address).run s =
      (s, if SDM.instructionDebug s.psr then [.debug address] else []) := by
  cases dynamic
  · rw [sound rfl]
    cases enabled : SDM.instructionDebug s.psr <;> rfl
  · simp only [emitDebug, SDM.instructionDebug, Effect.run, Expr.eval, read, if_true]
    split <;> rfl
