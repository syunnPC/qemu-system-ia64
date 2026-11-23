/- SPDX-License-Identifier: GPL-2.0-or-later -/
import TraceOrder.Spec

set_option warningAsError true

namespace TraceOrder.Forward

-- A unit is an entire aligned cell in the equal-width model or one
-- physical byte in the overlapping-width model.
structure Image (Event Unit : Type) where
  value : Event → Unit → Option Nat
  initial : Unit → Nat

variable {Event Cpu Loc Unit : Type}

def At (m : Image Event Unit) (e : Event) (u : Unit) : Prop :=
  (m.value e u).isSome = true

def WriteAt (x : Execution Event Cpu Loc) (m : Image Event Unit)
    (w : Event) (u : Unit) : Prop := (x.event w).kind.write = true ∧ At m w u

def Own (x : Execution Event Cpu Loc) (m : Image Event Unit)
    (p : Cpu) (w : Event) (u : Unit) : Prop :=
  (x.event w).cpu = some p ∧ WriteAt x m w u

def Reader (x : Execution Event Cpu Loc) (m : Image Event Unit)
    (p : Cpu) (r : Event) (u : Unit) : Prop :=
  (x.event r).cpu = some p ∧ (x.event r).kind = .load ∧ At m r u

def LocallyVisible (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (w r : Event) (u : Unit) : Prop :=
  Own x m p w u ∧ v.localTime w < v.localTime r

def StillLocal (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (w r : Event) (u : Unit) : Prop :=
  LocallyVisible x v m p w r u ∧ v.localTime r < v.remoteTime p w

-- The TCG-side model allows a load to use an older same-vCPU store
-- whose publication is still pending. It selects the youngest of
-- those stores in program order. The target's read-source behavior
-- remains a premise of the refinement.
def Buffered (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (w r : Event) (u : Unit) : Prop :=
  Own x m p w u ∧ x.po w r ∧ v.localTime r < v.remoteTime p w

def PublishedBefore (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (w r : Event) (u : Unit) : Prop :=
  WriteAt x m w u ∧ v.remoteTime p w < v.localTime r

def LatestPublished (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (w r : Event) (u : Unit) : Prop :=
  PublishedBefore x v m p w r u ∧
    ∀ other, PublishedBefore x v m p other r u → ¬v.remoteTime p w < v.remoteTime p other

inductive IRValue (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (r : Event) (u : Unit) : Nat → Prop
  | forwarded (w : Event) (found : Buffered x v m p w r u)
      (youngest : ∀ other, Buffered x v m p other r u → ¬x.po w other)
      (value : m.value w u = some b) : IRValue x v m p r u b
  | published (w : Event) (nonePending : ∀ other, ¬Buffered x v m p other r u)
      (latest : LatestPublished x v m p w r u)
      (value : m.value w u = some b) : IRValue x v m p r u b
  | initial (nonePending : ∀ other, ¬Buffered x v m p other r u)
      (nonePublished : ∀ other, ¬PublishedBefore x v m p other r u) :
      IRValue x v m p r u (m.initial u)

-- Intel 251429-001 §3.3.6 RV1, RV2 and RV3. Once any write is still
-- local, RV1 selects the latest locally visible own write. Merely
-- selecting an arbitrary same-vCPU write can give the wrong value.
inductive GuestValue (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (p : Cpu) (r : Event) (u : Unit) : Nat → Prop
  | local (w : Event) (hasLocal : ∃ other, StillLocal x v m p other r u)
      (found : LocallyVisible x v m p w r u)
      (latest : ∀ other, LocallyVisible x v m p other r u → ¬v.localTime w < v.localTime other)
      (value : m.value w u = some b) : GuestValue x v m p r u b
  | published (w : Event) (noneLocal : ∀ other, ¬StillLocal x v m p other r u)
      (latest : LatestPublished x v m p w r u)
      (value : m.value w u = some b) : GuestValue x v m p r u b
  | initial (noneLocal : ∀ other, ¬StillLocal x v m p other r u)
      (nonePublished : ∀ other, ¬PublishedBefore x v m p other r u) :
      GuestValue x v m p r u (m.initial u)

-- The two equivalences use same-vCPU linear program order, distinct
-- operation times and MD:RAW/WAR/WAW for a common unit. Own writes
-- to that unit publish in local visibility order by COH (§3.3.5).
structure LocalOrder (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) : Prop where
  storeLoad : ∀ p w r u, Own x m p w u → Reader x m p r u →
    (x.po w r ↔ v.localTime w < v.localTime r)
  storeStore : ∀ p a b u, Own x m p a u → Own x m p b u →
    (x.po a b ↔ v.localTime a < v.localTime b)
  publishInOrder : ∀ p a b u, Own x m p a u → Own x m p b u →
    v.localTime a < v.localTime b → v.remoteTime p a < v.remoteTime p b

theorem buffered_iff_local (localOrder : LocalOrder x v m)
    (reader : Reader x m p r u) :
    Buffered x v m p w r u ↔ StillLocal x v m p w r u := by
  constructor
  · rintro ⟨own, before, pending⟩
    exact ⟨⟨own, (localOrder.storeLoad p w r u own reader).mp before⟩, pending⟩
  · rintro ⟨⟨own, earlier⟩, pending⟩
    exact ⟨own, (localOrder.storeLoad p w r u own reader).mpr earlier, pending⟩

theorem readValueRefines (localOrder : LocalOrder x v m) (reader : Reader x m p r u)
    (selected : IRValue x v m p r u b) : GuestValue x v m p r u b := by
  cases selected with
  | forwarded w found youngest value =>
    have localWrite := (buffered_iff_local localOrder reader).mp found
    apply GuestValue.local w ⟨w, localWrite⟩ localWrite.1 ?_ value
    intro other earlier newer
    have laterPublication := localOrder.publishInOrder p w other u found.1 earlier.1 newer
    have stillPending : StillLocal x v m p other r u :=
      ⟨earlier, Nat.lt_trans localWrite.2 laterPublication⟩
    exact youngest other ((buffered_iff_local localOrder reader).mpr stillPending)
      ((localOrder.storeStore p w other u found.1 earlier.1).mpr newer)
  | published w nonePending latest value =>
    exact GuestValue.published w
      (fun other localWrite => nonePending other ((buffered_iff_local localOrder reader).mpr localWrite))
      latest value
  | initial nonePending nonePublished =>
    exact GuestValue.initial
      (fun other localWrite => nonePending other ((buffered_iff_local localOrder reader).mpr localWrite))
      nonePublished

structure OrderedVisibility (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (order : Edge Event) : Prop where
  localBefore : ∀ a b, order a b → v.localTime a < v.localTime b
  writeBefore : WritesRespect x v order

theorem OrderedVisibility.narrow (edges : OrderedVisibility x v lowered)
    (includes : ∀ a b, wanted a b → lowered a b) : OrderedVisibility x v wanted :=
  ⟨fun a b h => edges.localBefore a b (includes a b h),
    fun p a b h => edges.writeBefore p a b (includes a b h)⟩

def AgreeOnOverlap (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) : Prop :=
  ∀ p q a b u, a ≠ b → WriteAt x m a u → WriteAt x m b u →
    (v.remoteTime p a < v.remoteTime p b ∨ v.remoteTime p b < v.remoteTime p a) ∧
    (v.remoteTime p a < v.remoteTime p b ↔ v.remoteTime q a < v.remoteTime q b)

def OwnOverlapOrder (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) : Prop :=
  ∀ owner observer a b u, Own x m owner a u → Own x m owner b u →
    v.localTime a < v.localTime b → v.remoteTime observer a < v.remoteTime observer b

-- Intel 251429-001 §3.3.5 COH. The result uses shared byte coverage
-- rather than any externally supplied whole-operand coherence edge.
theorem ownOverlapOrder (localOrder : LocalOrder x v m) (agreement : AgreeOnOverlap x v m) :
    OwnOverlapOrder x v m := by
  intro owner observer a b u first second earlier
  have distinct : a ≠ b := by
    intro same
    subst b
    exact Nat.lt_irrefl _ earlier
  exact (agreement owner observer a b u distinct first.2 second.2).2.mp
    (localOrder.publishInOrder owner a b u first second earlier)

-- This is an operational boundary contract. WO, release and
-- overlapping-unit publication agreement, and the correspondence of
-- observed load values to the store-buffer model are premises.
structure IRExecution (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) (ordered : Edge Event) : Prop where
  hazardOrder : LocalOrder x v m
  publicationAfterLocal : ∀ p w, (x.event w).kind.write = true →
    v.localTime w < v.remoteTime p w
  publisherFirst : ∀ p q w, p ≠ q → (x.event w).kind.write = true →
    (x.event w).cpu = some p → v.remoteTime p w < v.remoteTime q w
  coherence : ∀ p a b, x.co a b → v.remoteTime p a < v.remoteTime p b
  agreedReleases : AgreeOnWrites x v OrderedWrite
  agreedOverlap : AgreeOnOverlap x v m
  ordered : OrderedVisibility x v ordered
  readValue : ∀ p r u b, Reader x m p r u → m.value r u = some b → IRValue x v m p r u b

structure GuestExecution (x : Execution Event Cpu Loc) (v : Visibility Event Cpu)
    (m : Image Event Unit) : Prop where
  hazardOrder : LocalOrder x v m
  publicationAfterLocal : ∀ p w, (x.event w).kind.write = true →
    v.localTime w < v.remoteTime p w
  publisherFirst : ∀ p q w, p ≠ q → (x.event w).kind.write = true →
    (x.event w).cpu = some p → v.remoteTime p w < v.remoteTime q w
  coherence : ∀ p a b, x.co a b → v.remoteTime p a < v.remoteTime p b
  agreedReleases : AgreeOnWrites x v OrderedWrite
  agreedOverlap : AgreeOnOverlap x v m
  ownOverlap : OwnOverlapOrder x v m
  ordered : OrderedVisibility x v (GuestOrder x)
  readValue : ∀ p r u b, Reader x m p r u → m.value r u = some b → GuestValue x v m p r u b

theorem refinesGuest (includes : ∀ a b, GuestOrder x a b → ordered a b)
    (ir : IRExecution x v m ordered) : GuestExecution x v m :=
  ⟨ir.hazardOrder, ir.publicationAfterLocal, ir.publisherFirst, ir.coherence,
    ir.agreedReleases, ir.agreedOverlap, ownOverlapOrder ir.hazardOrder ir.agreedOverlap,
    ir.ordered.narrow includes,
    fun p r u b reader observed =>
      readValueRefines ir.hazardOrder reader (ir.readValue p r u b reader observed)⟩

-- A published acquire of the release carries the writer's earlier
-- store to the reader. This argument does not restrict local
-- forwarding at other reads. The last load must use the published
-- branch for this particular unit.
theorem releaseCarriesStore {data release acquire read old : Event}
    (guest : GuestExecution x v m)
    (writeBeforeRelease : GuestOrder x data release)
    (acquireBeforeRead : GuestOrder x acquire read)
    (dataWrite : WriteAt x m data u)
    (releaseWrite : (x.event release).kind.write = true)
    (publishedFlag : PublishedBefore x v m p release acquire flag)
    (lastPublished : LatestPublished x v m p old read u) :
    ¬v.remoteTime p old < v.remoteTime p data := by
  apply lastPublished.2 data
  exact ⟨dataWrite, Nat.lt_trans (guest.ordered.writeBefore p data release
    writeBeforeRelease dataWrite.1 releaseWrite)
      (Nat.lt_trans publishedFlag.2 (guest.ordered.localBefore acquire read acquireBeforeRead))⟩

-- When one published write is the latest for every unit of a read
-- and there are no forwarded units, no unit can come from another
-- write. The same visibility witness is used for the entire read.
theorem wholePublishedReadMatches (ir : IRExecution x v m ordered)
    (latest : ∀ u, Reader x m p r u → LatestPublished x v m p source r u)
    (nonePending : ∀ u, Reader x m p r u → ∀ w, ¬Buffered x v m p w r u) :
    ∀ u b, Reader x m p r u → m.value r u = some b → m.value source u = some b := by
  intro u b reader value
  have last := latest u reader
  cases ir.readValue p r u b reader value with
  | forwarded w buffered _ _ => exact (nonePending u reader w buffered).elim
  | published w _ observed contents =>
    by_cases same : source = w
    · simpa [same] using contents
    · rcases (ir.agreedOverlap p p source w u same last.1.1 observed.1.1).1 with before | after
      · exact (last.2 w observed.1 before).elim
      · exact (observed.2 source last.1 after).elim
  | initial _ nonePublished => exact (nonePublished source last.1).elim

end TraceOrder.Forward
