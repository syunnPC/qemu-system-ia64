/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace TcgMemory.DeferredAcquire

private def litmus : State Bool :=
  start (Program.start (fun cpu addr => if cpu then addr - 0x1000 else addr)
    (fun _ => 0) Program.localRegs
    (fun cpu =>
      if cpu then [Program.load .w1 10 5 true, Program.load .w4 12 4 false]
      else [Program.put .w4 6 4 false, Program.put .w1 7 5 true]))

private def issueLoads : List (Action Bool) := [.delay true, .delay true]
private def deliver : List (Action Bool) :=
  [.execute false, .execute false, .publish false 1, .publish false 0]

private def registersSeen (d : State Bool) : Word × Word :=
  let r := (d.memory.threads true).regs
  (r.gr 10, r.gr 12)

-- Both loads can be queued, but the younger load cannot complete
-- while the earlier acquire waits for its flag.
example : (run sourceMasks litmus issueLoads).isSome = true := rfl
example : (run sourceMasks litmus (issueLoads ++ [.finish true 0])).isNone = true := rfl

example : (run sourceMasks litmus
    (issueLoads ++ deliver ++ [.finish true 1, .finish true 0])).map registersSeen =
      some (1, 0x12345678) := rfl

private def weakenedAcquire : Masks :=
  { sourceMasks with acquire := sourceMasks.acquire &&& ~~~loadLoadMask }

example : (run weakenedAcquire litmus (issueLoads ++ [.finish true 0])).isNone = true := rfl

private def missingLL : Masks :=
  { weakenedAcquire with beforeLoad := weakenedAcquire.beforeLoad &&& ~~~loadLoadMask }

-- Dropping both LL sources allows the data load to finish early.
example : (run missingLL litmus
    (issueLoads ++ [.finish true 0] ++ deliver ++ [.finish true 0])).map registersSeen =
      some (1, 0) := rfl

example : (run sourceMasks litmus [.delay true, .execute true]).isNone = true := rfl

end TcgMemory.DeferredAcquire
