/- SPDX-License-Identifier: GPL-2.0-or-later -/

theorem raise_packet_with_code (code : BitVec 32) (fault : Fault) :
    cRaiseWithCode code fault =
      ⟨fault.bundle, fault.bundle, fault.raw, fault.slot, code, code, code⟩ := by
  dsimp only [cRaiseWithCode]
  congr 1 <;> bv_decide

theorem illegal_number : cIllegalNumber = 2 := by decide

theorem raise_packet (fault : Fault) :
    cRaise fault =
      ⟨fault.bundle, fault.bundle, fault.raw, fault.slot, 2, 2, 2⟩ := by
  rw [cRaise, illegal_number, raise_packet_with_code]
  rfl

theorem nativeOrdinary_matches (before : Before) : cNativeOrdinary before ↔ nativeOrdinary before := by
  dsimp only [cNativeOrdinary, nativeOrdinary]
  bv_decide

theorem collect_matches (psr : Word) : cCollect psr = SDM.collecting psr := by
  dsimp only [cCollect, SDM.collecting]
  bv_decide

theorem sentinel_matches (psr iva : Word) :
    cSentinel psr iva ↔ SDM.collecting psr = false ∧ iva = 0 := by
  dsimp only [cSentinel, SDM.collecting]
  bv_decide

theorem handlerPsr_matches (psr dcr : Word) :
    cHandlerPsr psr dcr = SDM.interruptionPsr psr dcr := by
  dsimp only [cHandlerPsr, SDM.interruptionPsr]
  bv_decide

theorem handlerIp_matches (iva : Word) (aligned : iva &&& 0x7fff = 0) :
    cHandlerIp iva = iva + 0x5400 := by
  dsimp only [cHandlerIp]
  bv_decide

theorem iip_matches (ip : Word) (aligned : ip &&& 15 = 0) : cIip ip = ip := by
  dsimp only [cIip]
  bv_decide

theorem ipa_matches (last : Word) : cIpa last = last := by
  dsimp only [cIpa] <;> bv_decide

theorem ipsr_matches (psr : Word) (slot : BitVec 32) :
    cIpsr psr (slot.setWidth 8) = SDM.withRestartSlot psr (slot.setWidth 2) := by
  dsimp only [cIpsr, SDM.withRestartSlot]
  bv_decide

theorem isr_matches (slot : BitVec 32) (collect inFlight : Bool) (valid : slot < 3) :
    cIsr (slot.setWidth 8) collect inFlight = SDM.illegalIsr (slot.setWidth 2) (!collect || inFlight) := by
  dsimp only [cIsr, SDM.illegalIsr]
  bv_decide

/-- The normal native delivery branch, after an unsuppressed ordinary
Illegal Operation request with effective control registers and valid
instruction history. -/
theorem illegal_entry_selected (before : Before) (fault : Fault)
    (bundleAligned : fault.bundle &&& 15 = 0)
    (ivaAligned : before.iva &&& 0x7fff = 0)
    (slotValid : fault.slot < 3)
    (ordinary : SDM.collecting before.psr = true ∨ before.iva ≠ 0) :
    observe before (cDelivery before (cRaise fault)) = SDM.illegalEntry before fault := by
  have vectors : ¬ cSentinel before.psr before.iva := by
    intro sentinel
    have ⟨notCollect, zeroIVA⟩ := (sentinel_matches _ _).mp sentinel
    rcases ordinary with collect | mapped
    · simp [collect] at notCollect
    · exact mapped zeroIVA
  rw [raise_packet]
  dsimp only [cDelivery]
  rw [if_neg vectors, collect_matches, handlerIp_matches _ ivaAligned, handlerPsr_matches,
      isr_matches _ _ _ slotValid]
  unfold observe SDM.illegalEntry SDM.savedState
  dsimp only
  split
  · rfl
  · split
    · rw [iip_matches _ bundleAligned, ipsr_matches, ipa_matches]
      rfl
    · rfl

-- The special zero-IVA, noncollecting bare-loader branch exits delivery.
theorem bare_loader_no_vector (before : Before) (fault : Fault)
    (noCollection : SDM.collecting before.psr = false) (zeroIVA : before.iva = 0) :
    (cDelivery before (cRaise fault)).vectorEntered = false ∧
      (cDelivery before (cRaise fault)).ip = fault.bundle := by
  have sentinel := (sentinel_matches before.psr before.iva).mpr ⟨noCollection, zeroIVA⟩
  rw [raise_packet]
  dsimp only [cDelivery]
  rw [if_pos sentinel]
  exact ⟨rfl, rfl⟩

def fromSlotRequest (request : IA64Slot.FaultRequest) : Fault :=
  ⟨request.address.bundle, request.address.slot.setWidth 32, request.raw⟩

theorem slot_request_packet (request : IA64Slot.FaultRequest) :
    cRaiseWithCode (request.code.setWidth 32) (fromSlotRequest request) =
      ⟨request.address.bundle, request.address.bundle, request.raw, request.address.slot.setWidth 32,
        request.code.setWidth 32, request.code.setWidth 32, request.code.setWidth 32⟩ := by
  rw [raise_packet_with_code]
  rfl

theorem slot_request_enters (before : Before) (request : IA64Slot.FaultRequest)
    (illegal : request.code = 2) (native : nativeOrdinary before)
    (bundleAligned : request.address.bundle &&& 15 = 0) (ivaAligned : before.iva &&& 0x7fff = 0)
    (slotValid : request.address.slot < 3)
    (ordinary : SDM.collecting before.psr = true ∨ before.iva ≠ 0) :
    (cDispatch before (cRaiseWithCode (request.code.setWidth 32) (fromSlotRequest request))).map
      (observe before) = some (SDM.illegalEntry before (fromSlotRequest request)) := by
  have select := (nativeOrdinary_matches before).mpr native
  rw [illegal, ← illegal_number]
  change (cDispatch before (cRaise (fromSlotRequest request))).map (observe before) = _
  have index : (cRaise (fromSlotRequest request)).exceptionIndex = cIllegalNumber.setWidth 32 := by
    rw [raise_packet, illegal_number]
    rfl
  rw [cDispatch, if_pos ⟨index, select⟩]
  change some (observe before (cDelivery before (cRaise (fromSlotRequest request)))) = _
  have valid : (fromSlotRequest request).slot < 3 := by
    dsimp only [fromSlotRequest]
    bv_decide
  exact congrArg some
    (illegal_entry_selected before (fromSlotRequest request) bundleAligned ivaAligned valid ordinary)

theorem other_abi_code_skips_native_illegal (before : Before) (request : IA64Slot.FaultRequest)
    (other : request.code.setWidth 32 ≠ 2) :
    cDispatch before (cRaiseWithCode (request.code.setWidth 32) (fromSlotRequest request)) = none := by
  rw [slot_request_packet, cDispatch, illegal_number]
  exact if_neg fun selected => other selected.1

theorem other_slot_code_skips_native_illegal (before : Before) (request : IA64Slot.FaultRequest)
    (fits : request.code ≤ 0xffffffff) (other : request.code ≠ 2) :
    cDispatch before (cRaiseWithCode (request.code.setWidth 32) (fromSlotRequest request)) = none := by
  apply other_abi_code_skips_native_illegal
  bv_decide
