/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.Bypass

open Program (Command Machine Schedule Write)

inductive Event (Cpu : Type) where
  | localWrite (store : Write Cpu)
  | publication (store : Write Cpu)
  | read (cpu : Cpu) (slot address : Nat) (bytes : List Byte)
  | fence (cpu : Cpu)
  | silent

def localWrites [DecidableEq Cpu] (cpu : Cpu) : List (Event Cpu) → List (Write Cpu)
  | [] => []
  | .localWrite w :: tail =>
    if w.cpu = cpu then w :: localWrites cpu tail else localWrites cpu tail
  | _ :: tail => localWrites cpu tail

def publishedWrites : List (Event Cpu) → List (Write Cpu)
  | [] => []
  | .publication w :: tail => w :: publishedWrites tail
  | _ :: tail => publishedWrites tail

def ownWrites [DecidableEq Cpu] (cpu : Cpu) (published : List (Write Cpu)) : List (Write Cpu) :=
  published.filter (fun w => decide (w.cpu = cpu))

-- Intel 251429-001 §3.3.6. Histories are newest first and each
-- publication records one full store. Under FIFO publication the
-- unmatched prefix of a CPU's LV history has yet to reach owner RV.
def unmatched [DecidableEq Cpu] (cpu : Cpu) (events : List (Event Cpu)) : List (Write Cpu) :=
  let locals := localWrites cpu events
  locals.take (locals.length - (ownWrites cpu (publishedWrites events)).length)

def Covers (address : Nat) (w : Write Cpu) : Prop := (Program.payload w address).isSome = true

def Last (address : Nat) (w : Write Cpu) (history : List (Write Cpu)) : Prop :=
  ∃ newer older, history = newer ++ w :: older ∧
    ∀ other ∈ newer, ¬ Covers address other

inductive ReadValue [DecidableEq Cpu] (events : List (Event Cpu)) (initial : Nat → Byte)
    (cpu : Cpu) (address : Nat) : Byte → Prop
  | local (store : Write Cpu) (pending : ∃ w ∈ unmatched cpu events, Covers address w)
      (latest : Last address store (localWrites cpu events))
      (byte : Program.payload store address = some value) : ReadValue events initial cpu address value
  | published (store : Write Cpu) (noLocal : ∀ w ∈ unmatched cpu events, ¬ Covers address w)
      (latest : Last address store (publishedWrites events))
      (byte : Program.payload store address = some value) : ReadValue events initial cpu address value
  | initial (noLocal : ∀ w ∈ unmatched cpu events, ¬ Covers address w)
      (noPublication : ∀ w ∈ publishedWrites events, ¬ Covers address w) :
      ReadValue events initial cpu address (initial address)

structure Corresponds [DecidableEq Cpu] (m : Machine Cpu) (events : List (Event Cpu)) : Prop where
  publications : m.committed = publishedWrites events
  locals : ∀ cpu, (m.threads cpu).pending ++ ownWrites cpu m.committed = localWrites cpu events
  owner : ∀ cpu w, w ∈ (m.threads cpu).pending → w.cpu = cpu

theorem unmatchedCorrect [DecidableEq Cpu] (m : Machine Cpu) (events : List (Event Cpu))
    (h : Corresponds m events) (cpu : Cpu) : unmatched cpu events = (m.threads cpu).pending := by
  rw [unmatched, ← h.publications, ← h.locals cpu]
  simp

private theorem lastOfNewest (address : Nat) (before after : List (Write Cpu)) (w : Write Cpu)
    (byte : Byte) (found : Program.newest address before = some (w, byte)) :
    Last address w (before ++ after) ∧ Program.payload w address = some byte ∧ w ∈ before := by
  obtain ⟨newer, older, split, payload, miss⟩ := Program.newestSource address before w byte found
  exact ⟨⟨newer, older ++ after, by simp [split, List.append_assoc],
    fun other h => by simp [Covers, miss other h]⟩, payload, by simp [split]⟩

theorem sampledByte [DecidableEq Cpu] (m : Machine Cpu) (events : List (Event Cpu))
    (h : Corresponds m events) (cpu : Cpu) (address : Nat) :
    ReadValue events m.initial cpu address
      (Program.ordinaryByte m.initial m.committed (m.threads cpu).pending address) := by
  rw [Program.latestOwnElsePublished]
  cases pending : Program.newest address (m.threads cpu).pending with
  | some result =>
    obtain ⟨w, byte⟩ := result
    have ⟨latest, value, mem⟩ := lastOfNewest address _ (ownWrites cpu m.committed) w byte pending
    apply ReadValue.local w
    · exact ⟨w, by simpa [unmatchedCorrect m events h cpu] using mem, by simp [Covers, value]⟩
    · exact (h.locals cpu) ▸ latest
    · exact value
  | none =>
    have noLocal : ∀ w ∈ unmatched cpu events, ¬ Covers address w := by
      intro w member
      rw [unmatchedCorrect m events h cpu] at member
      simp [Covers, Program.noCover address _ pending w member]
    unfold Program.publishedByte
    cases published : Program.newest address m.committed with
    | none =>
      exact ReadValue.initial noLocal (fun w member => by
        rw [← h.publications] at member
        simp [Covers, Program.noCover address _ published w member])
    | some result =>
      obtain ⟨w, byte⟩ := result
      have ⟨latest, value, _⟩ := lastOfNewest address m.committed [] w byte published
      exact ReadValue.published w noLocal (by simpa [← h.publications] using latest) value

def Observed [DecidableEq Cpu] (events : List (Event Cpu)) (initial : Nat → Byte) :
    Event Cpu → Prop
  | .read cpu _ address bytes =>
    ∀ offset byte, bytes[offset]? = some byte → ReadValue events initial cpu (address + offset) byte
  | .publication w => w ∈ localWrites w.cpu events
  | .fence cpu => unmatched cpu events = []
  | _ => True

private def widthOf : Scalar → Width
  | .load op => op.width
  | .store op => op.width

private def sampledRequest (m : Machine Cpu) (cpu : Cpu) (r : Request) : Request :=
  let base := (m.phys cpu (getGR (m.threads cpu).regs (Encoded.baseRegister r.op))).toNat
  {r with cell := {r.cell with bytes :=
    fun i => Program.ordinaryByte m.initial m.committed (m.threads cpu).pending (base + i)}}

private theorem sampleMatches (m : Machine Cpu) (cpu : Cpu) (r : Request) :
    Program.sample m (m.threads cpu) cpu (.scalar r) =
      if Program.qualified m cpu (getGR (m.threads cpu).regs (Encoded.baseRegister r.op))
          (widthOf r.op) then some (.scalar (sampledRequest m cpu r)) else none := by
  cases r with
  | mk op cell => cases op <;> rfl

def effectEvent (m : Machine Cpu) (cpu : Cpu) (slot : Nat) : Option Effect → Event Cpu
  | some (.read va bytes _) => .read cpu slot (m.phys cpu va).toNat bytes
  | some (.write va bytes release) => .localWrite (Program.place m cpu slot va bytes release)
  | none => .silent

private def issuedObservation (m after : Machine Cpu) : Event Cpu :=
  match after.trace.drop m.trace.length with
  | [(owner, .scalar slot effect)] => effectEvent m owner slot (some effect)
  | _ => .silent

def sourceStep [DecidableEq Cpu] (m : Machine Cpu) :
    Schedule Cpu → Option (Machine Cpu × Event Cpu)
  | .publish cpu index => do
    let w ← (m.threads cpu).pending[index]?
    let after ← Program.publish Program.tcgPolicy m cpu index
    some (after, .publication w)
  | .issue cpu => do
    let t := m.threads cpu
    let after ← Program.issue Program.tcgPolicy tcgStep tcgAtomic m cpu
    match t.next with
    | .mf :: _ => some (after, .fence cpu)
    | .memory (.scalar _) :: _ => some (after, issuedObservation m after)
    | _ => none

private theorem lastPending (writes : List (Write Cpu)) (i : Nat) (w : Write Cpu)
    (found : writes[i]? = some w) (oldest : writes.drop (i + 1) = []) :
    writes.eraseIdx i ++ [w] = writes := by
  have atIndex := List.getElem?_eq_some_iff.mp found
  obtain ⟨bound, value⟩ := atIndex
  have split : writes.take i ++ w :: writes.drop (i + 1) = writes := by
    rw [← value, List.getElem_cons_drop bound]
    exact List.take_append_drop i writes
  simpa [List.eraseIdx_eq_take_drop_succ, oldest] using split

private theorem updateWriteState [DecidableEq Cpu] (m : Machine Cpu)
    (events : List (Event Cpu)) (h : Corresponds m events)
    (cpu : Cpu) (newThread : Program.Thread Cpu) (effect : Option Effect)
    (pending : newThread.pending =
      match effect with
      | some (.write va bytes release) =>
        Program.place m cpu (m.threads cpu).issued va bytes release :: (m.threads cpu).pending
      | _ => (m.threads cpu).pending) :
    Corresponds (m.update cpu newThread)
      (effectEvent m cpu (m.threads cpu).issued effect :: events) := by
  constructor
  · cases effect with
    | none => exact h.publications
    | some e => cases e <;> exact h.publications
  · intro observer
    cases effect with
    | none =>
      by_cases same : observer = cpu <;>
        simpa [Program.Machine.update, effectEvent, localWrites, same, pending] using h.locals observer
    | some e =>
      cases e with
      | read => by_cases same : observer = cpu <;>
          simpa [Program.Machine.update, effectEvent, localWrites, same, pending] using h.locals observer
      | write va bytes release =>
        by_cases same : observer = cpu
        · subst observer
          simp [Program.Machine.update, effectEvent, localWrites, pending, Program.place, h.locals cpu]
        · have reverse : cpu ≠ observer := Ne.symm same
          simpa [Program.Machine.update, effectEvent, localWrites, same, reverse, Program.place] using
            h.locals observer
  · intro observer w mem
    by_cases same : observer = cpu
    · subst observer
      have membership : w ∈ newThread.pending := by simpa [Program.Machine.update] using mem
      rw [pending] at membership
      cases effect with
      | none => exact h.owner cpu w membership
      | some e =>
        cases e with
        | read => exact h.owner cpu w membership
        | write va bytes release =>
          rcases List.mem_cons.mp membership with rfl | tail
          · rfl
          · exact h.owner cpu w tail
    · exact h.owner observer w (by simpa [Program.Machine.update, same] using mem)

private theorem readEffect [DecidableEq Cpu] (m : Machine Cpu)
    (events : List (Event Cpu)) (h : Corresponds m events) (cpu : Cpu) (r : Request) :
    Observed events m.initial
      (effectEvent m cpu (m.threads cpu).issued
        (tcgStep (m.threads cpu).regs (sampledRequest m cpu r)).effect) := by
  rw [stepMatches]
  cases r with
  | mk op cell =>
    cases op with
    | store st =>
      simp only [sampledRequest, specStep, specStore]
      repeat first | exact True.intro | split
    | load ld =>
      simp only [sampledRequest, specStep, specLoad]
      repeat first | exact True.intro | split
      all_goals
        intro offset byte read
        have bound : offset < ld.width.bytes := by
          simpa using (List.getElem?_eq_some_iff.mp read).1
        simp only [List.getElem?_map, List.getElem?_range bound, Option.map_some,
          Option.some.injEq] at read
        simp [Option.map] at read
        subst byte
        exact sampledByte m events h cpu _

private theorem publicationCorrect [DecidableEq Cpu] (m after : Machine Cpu)
    (events : List (Event Cpu)) (h : Corresponds m events) (cpu : Cpu) (i : Nat) (w : Write Cpu)
    (found : (m.threads cpu).pending[i]? = some w)
    (exec : Program.publish Program.tcgPolicy m cpu i = some after) :
    after.initial = m.initial ∧ Corresponds after (.publication w :: events) := by
  obtain ⟨allowed, _⟩ := Program.publishOnlyWhenAllowed m after cpu i w found exec
  have drained : ((m.threads cpu).pending.drop (i + 1)) = [] := by
    cases old : (m.threads cpu).pending.drop (i + 1) with
    | nil => rfl
    | cons first rest =>
      have wait := Program.ordinaryStoreWaits w first (first :: rest) (by simp)
      rw [old, wait] at allowed
      contradiction
  have oldest := lastPending _ i w found drained
  have owner := h.owner cpu w (List.mem_of_getElem? found)
  simp [Program.publish, found, allowed] at exec
  subst after
  refine ⟨rfl, ?_⟩
  constructor
  · simp [publishedWrites, h.publications]
  · intro observer
    by_cases same : observer = cpu
    · subst observer
      simp only [Program.Machine.update, if_pos rfl, ownWrites, List.filter_cons,
        owner, decide_true, if_true, localWrites]
      calc
        _ = ((m.threads cpu).pending.eraseIdx i ++ [w]) ++ ownWrites cpu m.committed :=
          (List.append_assoc _ [w] _).symm
        _ = localWrites cpu events := by rw [oldest]; exact h.locals cpu
    · have different : w.cpu ≠ observer := by simpa [owner] using Ne.symm same
      simpa [Program.Machine.update, same, ownWrites, different, localWrites] using h.locals observer
  · intro observer store member
    by_cases same : observer = cpu
    · subst observer
      simp only [Program.Machine.update, if_pos rfl] at member
      exact h.owner cpu store (List.mem_of_mem_eraseIdx member)
    · exact h.owner observer store (by simpa [Program.Machine.update, same] using member)

private theorem fenceState [DecidableEq Cpu] (m after : Machine Cpu)
    (events : List (Event Cpu)) (h : Corresponds m events) (cpu : Cpu) (rest : List Command)
    (picked : (m.threads cpu).next = .mf :: rest)
    (issued : Program.issue Program.tcgPolicy tcgStep tcgAtomic m cpu = some after) :
    after.initial = m.initial ∧ Corresponds after (.fence cpu :: events) := by
  unfold Program.issue at issued
  simp only [picked] at issued
  split at issued
  · contradiction
  · split at issued
    · contradiction
    · cases issued
      refine ⟨rfl, h.publications, ?_, ?_⟩
      · intro observer
        by_cases same : observer = cpu <;>
          simpa [Program.Machine.update, same, localWrites] using h.locals observer
      · intro observer w member
        by_cases same : observer = cpu <;>
          exact h.owner observer w (by simpa [Program.Machine.update, same] using member)

private theorem scalarState [DecidableEq Cpu] (m after : Machine Cpu)
    (events : List (Event Cpu)) (h : Corresponds m events) (cpu : Cpu) (r : Request)
    (rest : List Command) (picked : (m.threads cpu).next = .memory (.scalar r) :: rest)
    (issued : Program.issue Program.tcgPolicy tcgStep tcgAtomic m cpu = some after) :
    after.initial = m.initial ∧
    issuedObservation m after = effectEvent m cpu (m.threads cpu).issued
      (tcgStep (m.threads cpu).regs (sampledRequest m cpu r)).effect ∧ Corresponds after
      (effectEvent m cpu (m.threads cpu).issued
        (tcgStep (m.threads cpu).regs (sampledRequest m cpu r)).effect :: events) := by
  unfold Program.issue at issued
  simp only [picked, sampleMatches] at issued
  split at issued
  · contradiction
  · split at issued
    · simp only [Option.bind_some, memoryStep, Option.some.injEq] at issued
      cases issued
      let effect := (tcgStep (m.threads cpu).regs (sampledRequest m cpu r)).effect
      have pending :
          (Program.record m (m.threads cpu) cpu
            (effect.toList.map (MemoryRecord.scalar (m.threads cpu).issued))).1 =
          match effect with
          | some (.write va bytes release) =>
            Program.place m cpu (m.threads cpu).issued va bytes release :: (m.threads cpu).pending
          | _ => (m.threads cpu).pending := by
        cases effect with
        | none => rfl
        | some e => cases e <;> rfl
      have committed :
          (Program.record m (m.threads cpu) cpu
            (effect.toList.map (MemoryRecord.scalar (m.threads cpu).issued))).2 = m.committed := by
        cases effect with
        | none => rfl
        | some e => cases e <;> rfl
      rw [committed]
      refine ⟨rfl, ?_, ?_⟩
      · simp only [issuedObservation, List.drop_left]
        cases outcome : (tcgStep (m.threads cpu).regs (sampledRequest m cpu r)).effect with
        | none => rfl
        | some e => cases e <;> rfl
      let new := (tcgStep (m.threads cpu).regs (sampledRequest m cpu r))
      let thread : Program.Thread Cpu :=
        ⟨new.regs, rest, (m.threads cpu).issued + 1,
          (Program.record m (m.threads cpu) cpu
            (effect.toList.map (MemoryRecord.scalar (m.threads cpu).issued))).1,
          if new.exit == .success || new.exit == .nullified then none else some new.exit⟩
      have state := updateWriteState m events h cpu thread effect pending
      exact ⟨state.publications, state.locals, state.owner⟩
    · contradiction

theorem stepCorrect [DecidableEq Cpu] (m after : Machine Cpu) (events : List (Event Cpu))
    (h : Corresponds m events) (action : Schedule Cpu) (event : Event Cpu)
    (took : sourceStep m action = some (after, event)) :
    after.initial = m.initial ∧ Observed events m.initial event ∧
      Corresponds after (event :: events) := by
  cases action with
  | publish cpu index =>
    cases queued : (m.threads cpu).pending[index]? with
    | none => simp [sourceStep, queued] at took
    | some w =>
      cases exec : Program.publish Program.tcgPolicy m cpu index with
      | none => simp [sourceStep, queued, exec] at took
      | some actual =>
        have pair : actual = after ∧ Event.publication w = event := by
          simpa [sourceStep, queued, exec] using took
        obtain ⟨rfl, rfl⟩ := pair
        have inQueue := List.mem_of_getElem? queued
        have owner := h.owner cpu w inQueue
        have seen : w ∈ localWrites w.cpu events := by
          rw [owner, ← h.locals cpu]
          exact List.mem_append_left _ inQueue
        obtain ⟨unchanged, next⟩ := publicationCorrect m actual events h cpu index w queued exec
        exact ⟨unchanged, seen, next⟩
  | issue cpu =>
    cases exec : Program.issue Program.tcgPolicy tcgStep tcgAtomic m cpu with
    | none => simp [sourceStep, exec] at took
    | some actual =>
      cases picked : (m.threads cpu).next with
      | nil => simp [sourceStep, exec, picked] at took
      | cons command rest =>
        cases command with
        | mf =>
          have pair : actual = after ∧ Event.fence cpu = event := by
            simpa [sourceStep, exec, picked] using took
          obtain ⟨rfl, rfl⟩ := pair
          have drains : (m.threads cpu).pending = [] := by
            cases queued : (m.threads cpu).pending with
            | nil => rfl
            | cons first rest =>
              have blocked := Program.fenceDrains m cpu _ picked (by simp [queued])
              rw [blocked] at exec
              contradiction
          obtain ⟨unchanged, next⟩ := fenceState m actual events h cpu rest picked exec
          exact ⟨unchanged, (unmatchedCorrect m events h cpu).trans drains, next⟩
        | memory request =>
          cases request with
          | semaphore => simp [sourceStep, exec, picked] at took
          | scalar r =>
            have pair : actual = after ∧
                issuedObservation m actual = event := by
              simpa [sourceStep, exec, picked] using took
            obtain ⟨rfl, rfl⟩ := pair
            obtain ⟨unchanged, seen, next⟩ := scalarState m actual events h cpu r rest picked exec
            exact ⟨unchanged, seen.symm ▸ readEffect m events h cpu r, seen.symm ▸ next⟩

def run [DecidableEq Cpu] (m : Machine Cpu) (events : List (Event Cpu)) :
    List (Schedule Cpu) → Option (Machine Cpu × List (Event Cpu))
  | [] => some (m, events)
  | action :: later => do
    let (after, event) ← sourceStep m action
    run after (event :: events) later

-- Each read is checked against the prefix visible at that step. The
-- read and publication are indivisible actions in this WB projection.
inductive Valid [DecidableEq Cpu] (initial : Nat → Byte) : List (Event Cpu) → Prop
  | nil : Valid initial []
  | next {observation : Event Cpu} :
      Valid initial before → Observed before initial observation → Valid initial (observation :: before)

private def plainStep [DecidableEq Cpu] (m : Machine Cpu) : Schedule Cpu → Option (Machine Cpu)
  | .issue cpu => Program.issue Program.tcgPolicy tcgStep tcgAtomic m cpu
  | .publish cpu index => Program.publish Program.tcgPolicy m cpu index

private theorem stepErases [DecidableEq Cpu] (m after : Machine Cpu) (action : Schedule Cpu)
    (event : Event Cpu) (took : sourceStep m action = some (after, event)) :
    plainStep m action = some after := by
  cases action with
  | issue cpu =>
    unfold plainStep
    cases exec : Program.issue Program.tcgPolicy tcgStep tcgAtomic m cpu with
    | none => simp [sourceStep, exec] at took
    | some actual =>
      simp only [sourceStep, exec, Option.bind_some] at took
      split at took <;> simp_all
  | publish cpu index =>
    unfold plainStep
    cases exec : Program.publish Program.tcgPolicy m cpu index with
    | none =>
      cases q : (m.threads cpu).pending[index]? <;> simp [sourceStep, exec, q] at took
    | some actual =>
      cases q : (m.threads cpu).pending[index]? with
      | none => simp [sourceStep, q] at took
      | some w =>
        have same : actual = after ∧ Event.publication w = event := by
          simpa [sourceStep, exec, q] using took
        exact exec.trans (congrArg some same.1)

theorem runCorrect [DecidableEq Cpu] (m final : Machine Cpu)
    (events result : List (Event Cpu)) (h : Corresponds m events) (valid : Valid m.initial events)
    (actions : List (Schedule Cpu)) (ran : run m events actions = some (final, result)) :
    Valid m.initial result ∧ Corresponds final result ∧ Program.sourceRun m actions = some final := by
  induction actions generalizing m events with
  | nil =>
    simp only [run, Option.some.injEq, Prod.mk.injEq] at ran
    obtain ⟨rfl, rfl⟩ := ran
    exact ⟨valid, h, rfl⟩
  | cons action rest ih =>
    cases took : sourceStep m action with
    | none => simp [run, took] at ran
    | some pair =>
      obtain ⟨after, event⟩ := pair
      have nextRun : run after (event :: events) rest = some (final, result) := by
        simpa [run, took] using ran
      obtain ⟨unchanged, observation, invariant⟩ := stepCorrect m after events h action event took
      have validNext : Valid after.initial (event :: events) := by
        rw [unchanged]
        exact Valid.next valid observation
      obtain ⟨all, finished, original⟩ := ih after (event :: events) invariant validNext nextRun
      have exec := stepErases m after action event took
      refine ⟨unchanged ▸ all, finished, ?_⟩
      change (plainStep m action).bind (fun after => Program.sourceRun after rest) = some final
      rw [exec]
      exact original

def emittedLoadAllowsBypass : Bool :=
  DeferredAcquire.defaultMask atomicLoadRequest 0 &&& storeLoadMask == 0

theorem emittedLoadDoesNotDrain : emittedLoadAllowsBypass = true := rfl

def sourceBundles [DecidableEq Cpu]
    (phys : Cpu → Word → Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : List (Cpu × List Encoded.SlotInput)) (actions : List (Schedule Cpu)) :
    Option (Machine Cpu × List (Event Cpu)) :=
  if emittedLoadAllowsBypass then
    (Encoded.initialMachine Encoded.emittedCommand phys initial regs programs).bind
      (fun m => run m [] actions)
  else none

theorem started [DecidableEq Cpu] (phys : Cpu → Word → Word) (initial : Nat → Byte)
    (regs : Cpu → Registers) (programs : List (Cpu × List Encoded.SlotInput)) (m : Machine Cpu)
    (loaded : Encoded.initialMachine Encoded.emittedCommand phys initial regs programs = some m) :
    m.initial = initial ∧ Corresponds m [] := by
  cases parse : programs.mapM (fun (cpu, slots) =>
      (slots.mapM Encoded.emittedCommand).map (Prod.mk cpu)) with
  | none => simp [Encoded.initialMachine, parse] at loaded
  | some rows =>
    simp only [Encoded.initialMachine, parse] at loaded
    have selected : some (Program.start phys initial regs
        (fun cpu => (rows.lookup cpu).getD [])) = some m := loaded
    injection selected with same
    subst m
    exact ⟨rfl, rfl, fun _ => rfl, fun _ _ impossible => by cases impossible⟩

theorem sourceBundlesCorrect [DecidableEq Cpu]
    (phys : Cpu → Word → Word) (initial : Nat → Byte) (regs : Cpu → Registers)
    (programs : List (Cpu × List Encoded.SlotInput)) (actions : List (Schedule Cpu))
    (final : Machine Cpu) (log : List (Event Cpu))
    (ran : sourceBundles phys initial regs programs actions = some (final, log)) :
    Valid initial log ∧ Corresponds final log ∧
      (Encoded.initialMachine Encoded.guestCommand phys initial regs programs).bind
        (fun m => Program.guestRun m actions) = some final := by
  unfold sourceBundles at ran
  simp only [emittedLoadDoesNotDrain, if_true] at ran
  cases loaded : Encoded.initialMachine Encoded.emittedCommand phys initial regs programs with
  | none => simp [loaded] at ran
  | some m =>
    obtain ⟨base, invariant⟩ := started phys initial regs programs m loaded
    obtain ⟨valid, finalInvariant, exec⟩ :=
      runCorrect m final [] log invariant .nil actions (by simpa [loaded] using ran)
    exact ⟨base ▸ valid, finalInvariant,
      Encoded.rawProgramRefines phys initial regs programs actions final (by simpa [loaded] using exec)⟩

-- Expand a common publication into consecutive observer RV operations.
-- The owner is first, as required by Intel §3.3.1 WO. No sampled read
-- occurs inside the expansion, so RV2 sees exactly the publication prefix.
def actionTime (cpuCount action : Nat) : Nat := (cpuCount + 2) * action

def remoteTime {n : Nat} (owner observer : Fin n) (publication : Nat) : Nat :=
  actionTime n publication + if observer = owner then 1 else observer.val + 2

private theorem remoteInPhase {n : Nat} (owner observer : Fin n) (publication : Nat) :
    actionTime n publication < remoteTime owner observer publication ∧
    remoteTime owner observer publication < actionTime n (publication + 1) := by
  have bound := observer.isLt
  unfold remoteTime actionTime
  simp only [Nat.mul_succ]
  split <;> omega

theorem ownerBeforeObservers {n : Nat} (owner observer : Fin n) (publication : Nat)
    (other : observer ≠ owner) : remoteTime owner owner publication < remoteTime owner observer publication := by
  simp [remoteTime, other]

theorem publicationVisibleExactly {n : Nat} (owner observer : Fin n) (publication read : Nat) :
    remoteTime owner observer publication < actionTime n read ↔ publication < read := by
  have range := remoteInPhase owner observer publication
  constructor
  · intro seen
    by_cases earlier : publication < read
    · exact earlier
    · have noLater : actionTime n read ≤ actionTime n publication :=
        Nat.mul_le_mul_left _ (Nat.le_of_not_gt earlier)
      omega
  · intro earlier
    have bound : actionTime n (publication + 1) ≤ actionTime n read :=
      Nat.mul_le_mul_left _ earlier
    omega

theorem publicationsAgree {n : Nat} (a b p q : Fin n) (first second : Nat)
    (distinct : first ≠ second) :
    remoteTime a p first < remoteTime b p second ↔
      remoteTime a q first < remoteTime b q second := by
  have ap := remoteInPhase a p first
  have aq := remoteInPhase a q first
  have bp := remoteInPhase b p second
  have bq := remoteInPhase b q second
  rcases Nat.lt_or_gt_of_ne distinct with earlier | later
  · have before : actionTime n (first + 1) ≤ actionTime n second :=
      Nat.mul_le_mul_left _ earlier
    exact ⟨fun _ => by omega, fun _ => by omega⟩
  · have before : actionTime n (second + 1) ≤ actionTime n first :=
      Nat.mul_le_mul_left _ later
    exact ⟨fun impossible => by omega, fun impossible => by omega⟩

theorem contract :
    HostOrder.Contract ∧ Encoded.Contract ∧ emittedLoadAllowsBypass = true ∧
    (∀ {Cpu : Type} [DecidableEq Cpu] (phys : Cpu → Word → Word) (initial : Nat → Byte)
      (regs : Cpu → Registers) (programs : List (Cpu × List Encoded.SlotInput))
      (actions : List (Schedule Cpu)) (final : Machine Cpu) (log : List (Event Cpu)),
      sourceBundles phys initial regs programs actions = some (final, log) →
      Valid initial log ∧ Corresponds final log ∧
      (Encoded.initialMachine Encoded.guestCommand phys initial regs programs).bind
        (fun m => Program.guestRun m actions) = some final) ∧
    (∀ {n : Nat} (owner observer : Fin n) (publication : Nat),
      observer ≠ owner → remoteTime owner owner publication < remoteTime owner observer publication) ∧
    (∀ {n : Nat} (owner observer : Fin n) (publication read : Nat),
      remoteTime owner observer publication < actionTime n read ↔ publication < read) ∧
    (∀ {n : Nat} (a b p q : Fin n) (first second : Nat),
      first ≠ second → (remoteTime a p first < remoteTime b p second ↔
        remoteTime a q first < remoteTime b q second)) :=
  ⟨HostOrder.verified, Encoded.contract, emittedLoadDoesNotDrain, sourceBundlesCorrect, ownerBeforeObservers,
    publicationVisibleExactly, publicationsAgree⟩

private def physical (cpu : Fin 3) (address : Word) : Word :=
  address - BitVec.ofNat 64 (0x1000 * cpu.val)

private def initial (address : Nat) : Byte := BitVec.ofNat 8 (0x80 + address - 16)

private def registers (cpu : Fin 3) : Registers :=
  let defaults := Program.localRegs false
  let page := BitVec.ofNat 64 (0x1000 * cpu.val)
  { defaults with gr := fun r =>
      if r = 4 then page + 16
      else if r = 5 then page + 18
      else if r = 6 then page + 20
      else if r = 7 then 0x13121110
      else if r = 8 then 0x2221
      else if r = 9 then 0xa1a0
      else 0 }

private def bundle (mode : BitVec 6) (base reg : BitVec 7) : Encoded.SlotInput :=
  let payload := 4#4 ++ 0#1 ++ mode ++ 0#3 ++ base ++
    (if 48 ≤ mode then reg ++ 0#7 else 0#7 ++ reg) ++ 0#6
  ⟨payload.zeroExtend 64 <<< (5 : Nat), 0, 0, 0, Program.cell⟩

private def programs : List (Fin 3 × List Encoded.SlotInput) :=
  [(0, [bundle 0x32 4 7, bundle 0x31 5 8, bundle 0x17 4 10, bundle 0x03 4 11]),
   (1, [bundle 0x31 6 9]),
   (2, [bundle 0x03 4 10, bundle 0x03 4 11, bundle 0x03 4 12])]

private def beforeDraining : List (Schedule (Fin 3)) :=
  [.issue 1, .publish 1 0, .issue 0, .issue 0, .issue 0, .issue 2]

private def partialPublication : List (Schedule (Fin 3)) :=
  beforeDraining ++ [.publish 0 1, .issue 2]

private def allPublished : List (Schedule (Fin 3)) :=
  partialPublication ++ [.publish 0 0, .issue 0, .issue 2]

-- The first observer assembles one LD8 using both local stores, the
-- remote ST2, and untouched initial bytes. The other observer sees
-- its two versions only when each full store actually publishes.
example : ∃ state log, sourceBundles physical initial registers programs allPublished =
    some (state, log) ∧ Valid initial log ∧
    (state.threads 0).regs.gr 10 = 0x8786a1a022211110 ∧
    (state.threads 0).regs.gr 11 = 0x8786a1a022211110 ∧
    (state.threads 2).regs.gr 10 = 0x8786a1a083828180 ∧
    (state.threads 2).regs.gr 11 = 0x8786a1a013121110 ∧
    (state.threads 2).regs.gr 12 = 0x8786a1a022211110 ∧
    (state.threads 0).pending = [] := by
  let result := ((Encoded.initialMachine Encoded.guestCommand physical initial registers programs).bind
    (fun m => run m [] allPublished)).get (by rfl)
  have ran : sourceBundles physical initial registers programs allPublished = some result := by
    unfold sourceBundles
    rw [show Encoded.emittedCommand = Encoded.guestCommand from funext Encoded.rawCommandMatches]
    rfl
  exact ⟨result.1, result.2, ran, (sourceBundlesCorrect _ _ _ _ _ _ _ ran).1,
    rfl, rfl, rfl, rfl, rfl, rfl⟩

example : (sourceBundles physical initial registers programs
    [.issue 0, .issue 0, .publish 0 0]).isNone = true := by
  unfold sourceBundles
  rw [show Encoded.emittedCommand = Encoded.guestCommand from funext Encoded.rawCommandMatches]
  rfl

private def fenceStart : Machine Bool :=
  Program.start (fun _ addr => addr) (fun _ => 0) Program.localRegs
    (fun cpu => if cpu then [] else [Program.put .w4 6 4 false, .mf, Program.load .w4 11 4 true])

example : (run fenceStart [] [.issue false, .issue false]).isNone = true := rfl

example : (run fenceStart [] [.issue false, .publish false 0, .issue false, .issue false]).map
    (fun (machine, events) => ((machine.threads false).regs.gr 11,
      (unmatched false events).length)) = some (0x12345678, 0) := rfl

-- Intel 251429-001 Appendix A.6. Both acquires see their own release
-- before its publication. Each subsequent remote load still sees zero.
private def pairedReleases : List (Fin 3 × List Encoded.SlotInput) :=
  [(0, [bundle 0x34 4 13, bundle 0x14 4 10, bundle 0x00 6 11]),
   (1, [bundle 0x34 6 13, bundle 0x14 6 10, bundle 0x00 4 11])]

private def pairedRegisters (cpu : Fin 3) : Registers :=
  { registers cpu with gr := fun r => if r = 13 then 1 else (registers cpu).gr r }

example : (sourceBundles physical (fun _ => 0) pairedRegisters pairedReleases
    [.issue 0, .issue 1, .issue 0, .issue 1, .issue 0, .issue 1,
     .publish 0 0, .publish 1 0]).map
    (fun (m, _) => ((m.threads 0).regs.gr 10, (m.threads 0).regs.gr 11,
      (m.threads 1).regs.gr 10, (m.threads 1).regs.gr 11)) = some (1, 0, 1, 0) := by
  unfold sourceBundles
  rw [show Encoded.emittedCommand = Encoded.guestCommand from funext Encoded.rawCommandMatches]
  rfl

end TcgMemory.Bypass
