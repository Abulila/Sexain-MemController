// version_buffer.cc
// Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>

#include "version_buffer.h"

using namespace std;

uint64_t VersionBuffer::NewBlock() {
  assert(!sets_[FREE_SLOT].empty());
  int i = *sets_[FREE_SLOT].begin();
  sets_[FREE_SLOT].erase(sets_[FREE_SLOT].begin());
  sets_[IN_USE_SLOT].insert(i);
  return At(i);
}

void VersionBuffer::FreeBlock(uint64_t mach_addr, BufferState bs) {
  int i = Index(mach_addr);
  set<int>::iterator it = sets_[bs].find(i);
  assert(it != sets_[bs].end());
  sets_[bs].erase(it);
  sets_[FREE_SLOT].insert(i);
}

void VersionBuffer::PinBlock(uint64_t mach_addr) {
  int i = Index(mach_addr);
  assert(sets_[FREE_SLOT].count(i) == 0);
  set<int>::iterator it = sets_[IN_USE_SLOT].find(i);
  assert(it != sets_[IN_USE_SLOT].end());
  sets_[IN_USE_SLOT].erase(it);
  sets_[BACKUP_SLOT].insert(i);
}

void VersionBuffer::FreeBackup() {
  for(set<int>::iterator it = sets_[BACKUP_SLOT].begin();
      it != sets_[BACKUP_SLOT].end(); ++it) {
    sets_[FREE_SLOT].insert(*it);
  }
  sets_[BACKUP_SLOT].clear();
  assert(sets_[IN_USE_SLOT].size() + sets_[FREE_SLOT].size() == length_);
}

