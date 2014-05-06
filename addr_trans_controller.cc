// addr_trans_controller.cc
// Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>

#include "addr_trans_controller.h"

using namespace std;

uint64_t AddrTransController::LoadAddr(uint64_t phy_addr) {
  assert(phy_addr < phy_limit());
  uint64_t phy_tag = att_.Tag(phy_addr);
  uint64_t mach_addr = att_.Translate(phy_addr, att_.Lookup(phy_tag, NULL));
  if (isDRAM(phy_addr)) {
    mem_store_->OnDRAMRead(mach_addr, att_.block_size());
  } else {
    mem_store_->OnNVMRead(mach_addr, att_.block_size());
  }
  return mach_addr;
}

uint64_t AddrTransController::NVMStore(uint64_t phy_addr, int size) {
  const uint64_t phy_tag = att_.Tag(phy_addr);
  int index;
  att_.Lookup(phy_tag, &index);
  if (!in_ending()) {
    if (index != -EINVAL) { // found
      const ATTEntry& entry = att_.At(index);
      if (entry.IsPlaceholder() || entry.IsReset()) {
        uint64_t mach_base = ATTRevoke(index, size < att_.block_size());
        return att_.Translate(phy_addr, mach_base);
      } else if (entry.IsRegularDirty()) {
        return att_.Translate(phy_addr, entry.mach_base);
      } else {
        nvm_buffer_.PinBlock(entry.mach_base);
        ATTShrink(index, size < att_.block_size());
        return phy_addr;
      }
    } else { // not found
      assert(att_.GetLength(ATTEntry::DIRTY) < att_.length());
      const uint64_t mach_base = nvm_buffer_.NewBlock();
      if (att_.IsEmpty(ATTEntry::FREE)) {
        if (!att_.IsEmpty(ATTEntry::TEMP)) {
          int ti = att_.GetFront(ATTEntry::TEMP);
          ATTRevoke(ti);
        } else {
          int ci = att_.GetFront(ATTEntry::CLEAN);
          nvm_buffer_.PinBlock(att_.At(ci).mach_base);
          ATTShrink(ci);
        }
      }
      ATTSetup(phy_tag, mach_base, size, ATTEntry::DIRTY, ATTEntry::REGULAR);
      return att_.Translate(phy_addr, mach_base);
    }
  } else { // while checkpointing
    if (index != -EINVAL) { // found
      const ATTEntry& entry = att_.At(index);
      if (entry.IsRegularDirty() || entry.state == ATTEntry::TEMP) {
        return att_.Translate(phy_addr, entry.mach_base);
      } else {
        const uint64_t mach_base = dram_buffer_.NewBlock();
        nvm_buffer_.PinBlock(entry.mach_base);
        att_.Reset(index, mach_base, ATTEntry::TEMP, ATTEntry::CROSS);
        return att_.Translate(phy_addr, mach_base);
      }
    } else { // not found
      assert(att_.GetLength({ATTEntry::DIRTY, ATTEntry::TEMP}) < att_.length());
      if (!att_.IsEmpty(ATTEntry::FREE)) {
        const uint64_t mach_base = nvm_buffer_.NewBlock();
        ATTSetup(phy_tag, mach_base, size, ATTEntry::DIRTY, ATTEntry::REGULAR);
        return att_.Translate(phy_addr, mach_base);
      } else {
        const uint64_t mach_base = dram_buffer_.NewBlock();
        int ci = att_.GetFront(ATTEntry::CLEAN);
        const ATTEntry& entry = att_.At(ci);
        nvm_buffer_.PinBlock(entry.mach_base);
        mem_store_->Swap(att_.Addr(entry.phy_tag), entry.mach_base,
            att_.block_size());
        ATTShrink(ci, false);
        // suppose the NV-ATT has the replaced marked as swapped
        ATTSetup(phy_addr, mach_base, att_.block_size(),
            ATTEntry::DIRTY, ATTEntry::CROSS);
        return att_.Translate(phy_addr, mach_base);
      }
    }
  }
}

uint64_t AddrTransController::DRAMStore(uint64_t phy_addr, int size) {
  int index;
  uint64_t phy_tag = att_.Tag(phy_addr);
  att_.Lookup(phy_tag, &index);
  if (index != -EINVAL) { // found
    const ATTEntry& entry = att_.At(index);
    if (in_ending()) return att_.Translate(phy_addr, entry.mach_base);
    else {
      assert(entry.IsRegularTemp());
      return att_.Translate(phy_addr, ATTRevoke(index, false));
    }
  } else if (!in_ending()) {
    return phy_addr;
  } else { // in ending
    assert(att_.GetLength({ATTEntry::DIRTY, ATTEntry::TEMP}) < att_.length());
    const uint64_t mach_base = dram_buffer_.NewBlock();
    if (!att_.IsEmpty(ATTEntry::FREE)) {
      ATTSetup(phy_tag, mach_base, size, ATTEntry::TEMP, ATTEntry::REGULAR);
    } else {
      int ci = att_.GetFront(ATTEntry::CLEAN);
      const ATTEntry& entry = att_.At(ci);
      mem_store_->Swap(att_.Addr(entry.phy_tag), entry.mach_base, size);
      nvm_buffer_.PinBlock(entry.mach_base);
      ATTShrink(ci, false);
      ATTSetup(phy_tag, mach_base, size, ATTEntry::TEMP, ATTEntry::REGULAR);
      // suppose the mapping is marked in NV-ATT
    }
    return att_.Translate(phy_addr, mach_base);
  }
}

void AddrTransController::PseudoPageStore(uint64_t phy_addr) {
  int index = -EINVAL;
  uint64_t phy_tag = ptt_.Tag(phy_addr);
  uint64_t mach_base = ptt_.Lookup(phy_tag, &index);
  if (index != -EINVAL) {
    if (ptt_.At(index).state != ATTEntry::DIRTY) {
      assert(ptt_.At(index).state == ATTEntry::CLEAN);
      ptt_.FreeEntry(index);
      ptt_buffer_.PinBlock(mach_base);
    }
  } else {
    assert(ptt_.GetLength(ATTEntry::DIRTY) < ptt_.length());
    mach_base = ptt_buffer_.NewBlock();
    if (!ptt_.IsEmpty(ATTEntry::FREE)) {
      ptt_.Setup(phy_tag, mach_base, ATTEntry::DIRTY, ATTEntry::REGULAR);
    } else {
      int ci = ptt_.GetFront(ATTEntry::CLEAN);
      const ATTEntry& entry = ptt_.At(ci);
      mem_store_->Move(ptt_.Addr(entry.phy_tag), entry.mach_base,
          ptt_.block_size());
      ptt_buffer_.PinBlock(entry.mach_base);
      ptt_.FreeEntry(ci);
      ptt_.Setup(phy_tag, mach_base, ATTEntry::DIRTY, ATTEntry::REGULAR);
      ++num_page_move_;
    }
  }
}

uint64_t AddrTransController::StoreAddr(uint64_t phy_addr, int size) {
  assert(CheckValid(phy_addr, size) && phy_addr < phy_limit());
  if (isDRAM(phy_addr)) {
    PseudoPageStore(phy_addr);
    return DRAMStore(phy_addr, size);
  } else {
    return NVMStore(phy_addr, size);
  }
}

ATTControl AddrTransController::Probe(uint64_t phy_addr) {
  if (isDRAM(phy_addr)) {
    bool ptt_avail = ptt_.Contains(phy_addr) ||
        ptt_.GetLength(ATTEntry::DIRTY) < ptt_.length();
    if (in_ending()) {
      bool att_avail = att_.Contains(phy_addr) ||
          att_.GetLength({ATTEntry::DIRTY, ATTEntry::TEMP}) < att_.length();
      if (!att_avail || !ptt_avail) {
        return ATC_RETRY;
      }
    } else if (!ptt_avail) {
      return ATC_EPOCH;
    }
  } else { // NVM
    if (in_ending() && !att_.Contains(phy_addr) &&
        att_.GetLength({ATTEntry::DIRTY, ATTEntry::TEMP}) == att_.length()) {
      return ATC_RETRY;
    } else if (!in_ending() && !att_.Contains(phy_addr) &&
        att_.GetLength(ATTEntry::DIRTY) == att_.length()) {
      return ATC_EPOCH;
    }
  }
  return ATC_ACCEPT;
}

void AddrTransController::BeginEpochEnding() {
  assert(!in_ending());
  TempEntryRevoker temp_revoker(*this);
  att_.VisitQueue(ATTEntry::TEMP, &temp_revoker);
  assert(att_.IsEmpty(ATTEntry::TEMP)); 

  DirtyEntryRevoker dirty_revoker(*this);
  att_.VisitQueue(ATTEntry::DIRTY, &dirty_revoker);
  // suppose regular dirty entries are written back to NVM
  DirtyEntryCleaner att_cleaner(att_);
  att_.VisitQueue(ATTEntry::DIRTY, &att_cleaner);
  assert(att_.IsEmpty(ATTEntry::DIRTY));

  in_ending_ = true;

  // begin to deal with the next epoch
  DirtyEntryCleaner ptt_cleaner(ptt_);
  ptt_.VisitQueue(ATTEntry::DIRTY, &ptt_cleaner);
  assert(ptt_.IsEmpty(ATTEntry::DIRTY));

  ptt_buffer_.FreeBackup(); // without non-stall property 
  num_page_move_ = 0;
}

void AddrTransController::FinishEpochEnding() {
  assert(in_ending());
  nvm_buffer_.FreeBackup();
  in_ending_ = false;
}

void AddrTransController::ATTSetup(
    uint64_t phy_tag, uint64_t mach_base, int size,
    ATTEntry::State state, ATTEntry::SubState sub) {
  if (size < att_.block_size()) {
    mem_store_->Move(mach_base, att_.Addr(phy_tag), size);
  }
  att_.Setup(phy_tag, mach_base, state, sub);
}

void AddrTransController::ATTShrink(int index, bool move_data) {
  const ATTEntry& entry = att_.At(index);
  assert(entry.state == ATTEntry::CLEAN);
  if (move_data) {
    mem_store_->Move(
        att_.Addr(entry.phy_tag), entry.mach_base, att_.block_size()); 
  }
  att_.FreeEntry(index);
}

uint64_t AddrTransController::ATTRevoke(int index, bool move_data) {
  assert(!in_ending());
  const ATTEntry& entry = att_.At(index);
  uint64_t phy_base = att_.Addr(entry.phy_tag);
  if (entry.IsPlaceholder()) {
    uint64_t mach_base = nvm_buffer_.NewBlock();
    if (move_data) {
      mem_store_->Move(mach_base, entry.mach_base, att_.block_size());
    }
    dram_buffer_.FreeBlock(entry.mach_base, IN_USE_SLOT);
    att_.Reset(index, mach_base, ATTEntry::DIRTY, ATTEntry::REGULAR);
    return mach_base;
  } else if (entry.IsReset()) {
    if (move_data) {
      mem_store_->Move(phy_base, entry.mach_base, att_.block_size());
    }
    dram_buffer_.FreeBlock(entry.mach_base, IN_USE_SLOT);
    att_.FreeEntry(index);
    return phy_base;
  } else if (entry.IsRegularTemp()) {
    assert(isDRAM(phy_base));
    if (move_data) {
      mem_store_->Move(phy_base, entry.mach_base, att_.block_size());
    }
    dram_buffer_.FreeBlock(entry.mach_base, IN_USE_SLOT);
    att_.FreeEntry(index);
    return phy_base;
  } else assert(false);
}

