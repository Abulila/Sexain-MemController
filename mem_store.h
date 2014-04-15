// mem_store.h
// Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>

#ifndef SEXAIN_MEM_STORE_H_
#define SEXAIN_MEM_STORE_H_

#include <iostream>
#include <cstdint>
#include <cassert>

class MemStore {
 public:
  virtual void OnATTOperate() = 0;
  virtual void OnBufferOperate() = 0;

  virtual void OnNVMMove(uint64_t phy_addr, uint64_t mach_addr, int size) = 0;
  virtual void OnEpochEnd() = 0;

  virtual void OnNVMRead(uint64_t mach_addr, int size) = 0;
  virtual void OnNVMWrite(uint64_t mach_addr, int size) = 0;
  virtual void OnDRAMRead(uint64_t mach_addr, int size) = 0;
  virtual void OnDRAMWrite(uint64_t mach_addr, int size) = 0;
};

class TraceMemStore : public MemStore {
  void OnATTOperate() {
    std::cout << "[Info] ATT operation." << std::endl;
  }

  void OnBufferOperate() {
    std::cout << "[Info] VersionBuffer operation." << std::endl;
  }

  void OnNVMMove(uint64_t phy_addr, uint64_t mach_addr, int size) {
    std::cout << std::hex;
    std::cout << "[Info] NVM moves from " << mach_addr << " to " << phy_addr
        << std::endl;
    std::cout << std::dec;
  }

  void OnEpochEnd() {
    std::cout << "[Info] MemStore meets epoch end." << std::endl;
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
};

#endif // SEXAIN_MEM_STORE_H_

