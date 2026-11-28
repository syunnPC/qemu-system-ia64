# SPDX-License-Identifier: GPL-2.0-or-later
"""Project ordered ALLOC backing-store writes through the RAM wrappers."""

from Decode.check import get_match, uncomment
from Rse.frames import function, require


def ram_updates(root, source, header):
    store = function(source, "ia64_rse_store_one")
    endian_mask = get_match(r"#define IA64_RSC_BE\s+(0x[\da-fA-F]+)ULL",
                            header, "RSC endian mask")[1]
    marker = get_match(r"if\(ncb==(\d+)\)\{IA64RnatWritebackImage\*image=", store,
                       "RNAT store dispatch")[1]
    require(store, """
        uint64_t bspstore = env->ar_bspstore;
        uint64_t collection_addr = ia64_rse_collect_word(bspstore);
        uint32_t ncb = ia64_rse_collect_bit(bspstore);
        ia64_rse_rnat_bind(env, collection_addr);
    """, "spill derives the address and tag bit from BSPSTORE")
    require(store, """
        uint32_t p =
            ia64_rse_wrap_phys((int32_t)env->rse.rse_bol - env->rse.rse_dirty);
        ia64_rse_write_u64(env, bspstore, env->rse.rse_pgr[p], ra);
    """, "spill data register comes from the oldest dirty physical slot")
    require(function(source, "ia64_rse_pgr_nat_get"),
            "return (env->rse.rse_pgr_nat[p / 64] >> (p % 64)) & 1;",
            "spill NaT follows the same physical slot", full=True)
    require(store, """
        uint64_t spill_defined = env->rse.rse_rnat_defined & INT64_MAX;
        uint64_t defined = spill_defined;
        uint64_t value = env->ar_rnat & spill_defined;
        uint64_t previous;
        uint64_t collection;
        if (image->valid && image->addr == collection_addr) {
            uint64_t saved_defined = image->defined & INT64_MAX;
            value = (image->value & saved_defined & ~spill_defined) | value;
            defined |= saved_defined;
    """, "spill and writeback RNAT overlay")
    require(store, """
        if (image->valid && image->addr == collection_addr) {
            ia64_rse_rnat_writeback_clear(env, "store-retire");
        }
        ia64_rse_rnat_shadow_remove(env, collection_addr, "store-discard");
        ia64_rse_load_rnat_stash(env, collection_addr);
        env->rse.rse_load_rnat = collection;
        env->rse.rse_load_rnat_addr = collection_addr;
        env->rse.rse_load_rnat_defined = INT64_MAX;
        env->rse.rse_load_rnat_valid = true;
        env->ar_rnat = 0;
    """, "successful collection consumes writeback and clears spill latch")
    require(function(source, "ia64_rse_rnat_bind"), """
        if (env->rse.rse_rnat_addr == collection_addr) {
            return;
        }
    """, "prebound RNAT latch")
    require(function(source, "ia64_rse_rnat_bind"), """
        uint64_t value = 0;
        uint64_t defined = 0;
        uint64_t shadow_value;
        uint64_t shadow_defined;
    """, "default latch for unseeded collection")
    require(function(source, "ia64_rse_write_u64"), """
        int mmu_idx = ia64_rse_mmu_index(env);
        ia64_rse_check_data_debug(env, addr, true);
        ia64_rse_access_begin(env);
        ia64_exec_store_mmuidx(env, addr, value, 8,
                              (env->ar_rsc & IA64_RSC_BE) != 0, mmu_idx, ra);
        ia64_rse_access_end(env);
        ia64_rse_complete_memory_reference(env);
    """, "payload RAM store is an eight-byte RSC-endian write", full=True)
    require(function(source, "ia64_rse_write_collection"), """
        int mmu_idx = ia64_rse_mmu_index(env);
        uint64_t stored;
        ia64_rse_check_data_debug(env, addr, true);
        ia64_rse_access_begin(env);
        stored = ia64_exec_rse_store_collection(env, addr, value, defined,
                                               (env->ar_rsc & IA64_RSC_BE) != 0,
                                               mmu_idx, previous, ra);
        ia64_rse_access_end(env);
        ia64_rse_complete_memory_reference(env);
        return stored;
    """, "RNAT write uses the same translated address and issuing return address", full=True)
    access = uncomment((root / "target/ia64/exec-access.c").read_text(encoding="utf-8"))
    require(function(access, "ia64_exec_store_mmuidx"), """
        case 8:
            if (big_endian) {
                cpu_stq_be_mmuidx_ra(env, addr, value, mmu_idx, ra);
            } else {
                cpu_stq_le_mmuidx_ra(env, addr, value, mmu_idx, ra);
            }
            break;
    """, "eight-byte RAM store dispatch")
    collection = function(access, "ia64_exec_rse_store_collection")
    require(collection, """
        g_assert((addr & 7) == 0);
        defined &= INT64_MAX;
        value &= defined;
        if (defined == INT64_MAX) {
            *previous = 0;
            ia64_exec_store_mmuidx(env, addr, value, 8, big_endian, mmu_idx, ra);
            return value;
        }
        flags = probe_access_full(env, addr, 8, MMU_DATA_STORE, mmu_idx, false,
                                  &host, &full, ra);
        writable_ram = memory_region_is_ram(full->section->mr) &&
                       !memory_region_is_protected(full->section->mr) &&
                       !full->section->readonly &&
                       !(full->slow_flags[MMU_DATA_STORE] &
                         (TLB_BSWAP | TLB_DISCARD_WRITE | TLB_MMIO));
    """, "RNAT partial preservation requires a write probe to ordinary RAM")
    require(collection, """
        if (writable_ram) {
            if (host == NULL) {
                host = qemu_map_ram_ptr(NULL, addr + full->xlat_offset);
            }
            old = big_endian ? ldq_be_p(host) : ldq_le_p(host);
    """, "RNAT old RAM image uses the same endian as the write")
    require(collection, """
        *previous = old;
        value = ((old & ~defined) | value) & INT64_MAX;
        ia64_exec_store_mmuidx(env, addr, value, 8, big_endian, mmu_idx, ra);
        return value;
    """, "RNAT final RAM write and reserved-bit mask")

    return f"""
def mandatorySpillNeedC (s : FrameState) (packed : U32) : Nat :=
  let growth : Int := ((allocFieldsC packed).1.toNat : Int) - s.cfm.sof.toNat
  (growth - s.invalid - s.clean).toNat

def spillBigEndianC (i : SpillRamInput) : Bool :=
  decide (i.rsc &&& {endian_mask} ≠ 0)

def fullRnatMaskC : Word := 0x7fffffffffffffff

def mergedCollectionC (spill saved : RnatBits) (old : Word) : Word :=
  let spillMask := spill.defined &&& fullRnatMaskC
  let retainedMask := saved.defined &&& fullRnatMaskC
  let combined := (saved.value &&& retainedMask &&& ~~~spillMask) ||| (spill.value &&& spillMask)
  let defined := (spillMask ||| retainedMask) &&& fullRnatMaskC
  let value := combined &&& defined
  if defined = fullRnatMaskC then value
  else ((old &&& ~~~defined) ||| value) &&& fullRnatMaskC

def updateRnatC (latch : RnatBits) (bit : U32) (tag : Bool) : RnatBits :=
  let mask := (1 : Word) <<< bit.toNat
  {{ value := if tag then latch.value ||| mask else latch.value &&& ~~~mask
    defined := latch.defined ||| mask }}

def spillRamAttemptC (i : SpillRamInput) (s : SpillRamState) : SpillAttempt :=
  let addr := s.counters.bspstore
  let ncb := collectBitC addr
  if i.storeAllowed addr = false then .fault addr
  else if ncb = {marker} then
    let previous := spillRamWord (s.ram addr) (spillBigEndianC i)
    let value := mergedCollectionC s.latch (savedAt s (collectWordC addr)) previous
    .committed .collection ⟨addr, value, (spillBigEndianC i)⟩
      {{ counters := spillCommitC s.counters i.hasClean .collection,
        latch := ⟨0, 0⟩, writeback := consumeWriteback s (collectWordC addr),
        ram := commitRamWord s addr (spillRamWord value (spillBigEndianC i)) }}
  else
    let p := wrapPhysC (i.bol - s.counters.dirty)
    let slot := i.physical p
    .committed .payload ⟨addr, slot.1, (spillBigEndianC i)⟩
      {{ s with
        counters := spillCommitC s.counters i.hasClean .payload
        latch := updateRnatC s.latch ncb slot.2
        ram := commitRamWord s addr (spillRamWord slot.1 (spillBigEndianC i)) }}

def spillRamTraceC (i : SpillRamInput) (s : SpillRamState) (needed : Nat) : Nat → SpillTrace
  | 0 =>
    if needed = 0 then ⟨[], .retired, retireSpillRam s⟩ else ⟨[], .fuelExhausted, s⟩
  | fuel + 1 =>
    if needed = 0 then ⟨[], .retired, retireSpillRam s⟩ else
    match spillRamAttemptC i s with
    | .fault addr => ⟨[], .fault addr, s⟩
    | .committed kind write next =>
      let remaining := match kind with
        | .payload => needed - 1
        | .collection => needed
      if i.interruptAfter write.address then ⟨[write], .interrupt write.address, next⟩
      else
        let later := spillRamTraceC i next remaining fuel
        {{ later with writes := write :: later.writes }}
"""
