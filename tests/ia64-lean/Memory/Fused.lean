/- SPDX-License-Identifier: GPL-2.0-or-later -/
import Lean.Elab.Tactic.Omega

set_option warningAsError true

namespace FusedRelease

-- Two locations in a single RAM span, initially zero, with one writer.
-- The producer writes 1 to the earlier location and then to the later
-- location. The consumer reads the later location with acquire semantics,
-- then reads the earlier location.
structure Visibility where
  entryFence : Nat
  writeEarlier : Nat
  writeLater : Nat
  readLater : Nat
  readEarlier : Nat
  returned : Nat

-- Intel SDM Vol. 2 §4.4.7, Tables 4-15 and 4-16. For this observer,
-- the second release makes the earlier store visible beforehand.
def guestOrdering (v : Visibility) : Prop :=
  v.writeEarlier < v.writeLater ∧ v.readLater < v.readEarlier

-- The bulk call has a preceding fence and establishes bytes on return.
-- Its contract supplies no visibility order between the two byte writes.
-- Keep the receiving CPU's acquire/load order in the host model as well.
def bulkOrdering (v : Visibility) : Prop :=
  v.entryFence < v.writeEarlier ∧ v.entryFence < v.writeLater ∧
    v.writeEarlier < v.returned ∧ v.writeLater < v.returned ∧
    v.readLater < v.readEarlier

def messagePassingFailure (v : Visibility) : Prop :=
  v.writeLater < v.readLater ∧ v.readEarlier < v.writeEarlier

theorem guest_forbids_messagePassingFailure (v : Visibility) :
    guestOrdering v → ¬messagePassingFailure v := by
  intro ⟨first, observer⟩ ⟨later, earlier⟩
  omega

theorem bulk_admits_messagePassingFailure :
    ∃ v, bulkOrdering v ∧ messagePassingFailure v := by
  refine ⟨⟨0, 4, 1, 2, 3, 5⟩, ?_⟩
  unfold bulkOrdering messagePassingFailure
  decide

end FusedRelease
