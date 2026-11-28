/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.Program

private def localRegs (cpu : Bool) : Registers :=
  { exampleRegs with gr := fun r =>
      if r = 4 then (if cpu then 0x1010 else 16)
      else if r = 5 then (if cpu then 0x1018 else 24)
      else if r = 6 then 0x12345678
      else if r = 7 then 1
      else if r = 8 then 0xbeef
      else 0 }

private def cell : CellInput :=
  {exampleCell with guestBig := false, bytes := fun _ => 0xff}

private def put (width : Width) (source base : Reg) (release : Bool) : Command :=
  .memory (.scalar ⟨.store ⟨0, source, base, width, release, none⟩, cell⟩)

private def load (width : Width) (target base : Reg) (acquire : Bool) : Command :=
  .memory (.scalar ⟨.load ⟨0, target, base, width, acquire, .none⟩, cell⟩)

private def swap : Command := .memory (.semaphore ⟨0, 11, 4, .exchange .w2 8⟩ cell true)

private def litmus : Machine Bool :=
  start (fun cpu addr => if cpu then addr - 0x1000 else addr) (fun _ => 0) localRegs
    (fun cpu => if cpu then [load .w1 10 5 true, swap, load .w4 12 4 false]
                else [put .w4 6 4 false, load .w1 9 4 false, put .w1 7 5 true])

private def issued : List (Schedule Bool) := [.issue false, .issue false, .issue false]

-- The issuing CPU can forward while the other CPU still sees RAM.
example : (sourceRun litmus issued).map
    (fun m => ((m.threads false).regs.gr 9, ordinaryByte m.initial m.committed
      (m.threads true).pending 16, m.committed.length)) = some (0x78, 0, 0) := rfl

-- The later, disjoint release cannot publish ahead of the data.
example : (sourceRun litmus (issued ++ [.publish false 0])).isNone = true := rfl

-- The receiver observes the release, exchanges the overlapping low
-- half of the data, then reads its own atomic update and the untouched high half.
private def delivered := issued ++
  [.publish false 1, .publish false 0, .issue true, .issue true, .issue true]

example : (sourceRun litmus delivered).map
    (fun m => ((m.threads true).regs.gr 10, (m.threads true).regs.gr 11,
      (m.threads true).regs.gr 12)) = some (1, 0x5678, 0x1234beef) := rfl

example (final : Machine Bool) (h : sourceRun litmus delivered = some final) :
    guestRun litmus delivered = some final := programRefines _ _ _ h

private def plainStores (second : Reg) : Machine Bool :=
  {litmus with threads := fun cpu =>
    {(litmus.threads cpu) with next := [put .w4 6 4 false, put .w2 8 second false]}}

-- The guest can reorder disjoint plain stores. Its overlapping-store
-- order and the default TCG store/store order both remain in force.
example : (guestRun (plainStores 4) [.issue false, .issue false, .publish false 0]).isNone = true :=
  rfl

example : (guestRun (plainStores 5) [.issue false, .issue false, .publish false 0]).map
    (fun m => publishedByte m.initial m.committed 24) = some 0xef := rfl

example : (sourceRun (plainStores 5) [.issue false, .issue false, .publish false 0]).isNone = true := rfl
example : (sourceRun (plainStores 5)
    [.issue false, .issue false, .publish false 1, .publish false 0]).isSome = true := rfl

private def fenced : Machine Bool :=
  {litmus with threads := fun cpu => {(litmus.threads cpu) with next := [put .w4 6 4 false, .mf]}}

example : (sourceRun fenced [.issue false, .issue false]).isNone = true := rfl
example : (sourceRun fenced [.issue false, .publish false 0, .issue false]).isSome = true := rfl

end TcgMemory.Program
