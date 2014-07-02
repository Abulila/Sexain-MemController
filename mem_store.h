// mem_store.h
// Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>

#ifndef SEXAIN_MEM_STORE_H_
#define SEXAIN_MEM_STORE_H_

#include <iostream>
#include <cstdint>
#include <cassert>

class MemStore {
 public:
  virtual void DoMove(uint64_t destination, uint64_t source, int size) = 0;
  virtual void DoSwap(uint64_t static_addr, uint64_t mach_addr, int size) = 0;
 
  virtual void OnATTOp() { }
  virtual void OnBufferOp() { }

  virtual void OnNVMRead(uint64_t mach_addr, int size) { }
  virtual void OnNVMWrite(uint64_t mach_addr, int size) { }
  virtual void OnDRAMRead(uint64_t mach_addr, int size) { }
  virtual void OnDRAMWrite(uint64_t mach_addr, int size) { }

  ///
  /// Before the checkpointing frame begins
  ///
  virtual void OnCheckpointing() = 0;
  ///
  /// When ATT is saturated in checkpointing and the write request has to wait
  ///
  virtual void OnWaiting() = 0;
  ///
  /// When the end of checkpointing indicates that the current epoch finishes
  ///
  virtual void OnEpochEnd() { };
};

class TraceMemStore : public MemStore {
  void DoMove(uint64_t destination, uint64_t source, int size) {
    std::cout << std::hex;
    std::cout << "[Info] Mem moves from " << source << " to " << destination
        << std::endl;
    std::cout << std::dec;
  }

  void DoSwap(uint64_t static_addr, uint64_t mach_addr, int size) {
    std::cout << std::hex;
    std::cout << "[Info] NVM swaps " << mach_addr << " and " << static_addr
        << std::endl;
    std::cout << std::dec;
  }

  void OnATTOp() {
    std::cout << "[Info] ATT operation." << std::endl;
  }

  void OnBufferOpe() {
    std::cout << "[Info] VersionBuffer operation." << std::endl;
  }

  void OnNVMRead(uint64_t mach_addr, int size) {
    std::cout << std::hex;
    std::cout << "[Info] MemStore reads NVM: " << mach_addr << std::endl;
    std::cout << std::dec;
  }

  void OnNVMWrite(uint64_t mach_addr, int size) {
    std::cout << std::hex;
    std::cout << "[Info] MemStore writes to NVM: " << mach_addr << std::endl;
    std::cout << std::dec;
  }

  void OnDRAMRead(uint64_t mach_addr, int size) {
    std::cout << std::hex;
    std::cout << "[Info] MemStore reads DRAM: " << mach_addr << std::endl;
    std::cout << std::dec;
  }

  void OnDRAMWrite(uint64_t mach_addr, int size) {
    std::cout << std::hex;
    std::cout << "[Info] MemStore writes to DRAM: " << mach_addr << std::endl;
    std::cout << std::dec;
  }

  void OnEpochEnd() {
    std::cout << "[Info] MemStore meets epoch end." << std::endl;
  }
};

#endif // SEXAIN_MEM_STORE_H_

