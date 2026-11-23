/- SPDX-License-Identifier: GPL-2.0-or-later -/

namespace RseSpec

inductive SpillWord where
  | payload
  | collection
  deriving DecidableEq

structure SpillCounters where
  bspstore : Word
  dirty : Int
  dirtyNat : Int
  clean : Int
  cleanNat : Int
  invalid : Int

inductive SpillResult where
  | retire (state : SpillCounters)
  | dataFault (state : SpillCounters)
  | interrupt (state : SpillCounters)
  | waiting (state : SpillCounters)

inductive SpillEvent where
  | store (kind : SpillWord) (interrupt : Bool)
  | dataFault

def payloadCount : List SpillWord → Nat
  | [] => 0
  | .payload :: tail => payloadCount tail + 1
  | .collection :: tail => payloadCount tail

-- A mandatory spill fault retains the committed prefix. A successful payload
-- store makes one physical GR available. An RNAT store advances BSPSTORE but
-- does not make a GR available. An interrupt window follows each commit.
def spillPrefix (state : SpillCounters) (hasClean : Bool) (words : List SpillWord) :
    SpillCounters :=
  let payloads : Int := payloadCount words
  let collections : Int := (words.length : Int) - payloads
  { bspstore := state.bspstore + BitVec.ofNat 64 (8 * words.length)
    dirty := state.dirty - payloads
    dirtyNat := state.dirtyNat - collections
    clean := state.clean + if hasClean then payloads else 0
    cleanNat := state.cleanNat + if hasClean then collections else 0
    invalid := state.invalid + if hasClean then 0 else payloads }

end RseSpec
