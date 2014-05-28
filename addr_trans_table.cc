// addr_trans_table.cc
// Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>

#include "addr_trans_table.h"

using namespace std;

pair<int, Addr> AddrTransTable::Lookup(Tag phy_tag) {
  unordered_map<Tag, int>::iterator it = tag_index_.find(phy_tag);
  if (it == tag_index_.end()) { // not hit
    return make_pair(-EINVAL, ToAddr(phy_tag));
  } else {
    ATTEntry& entry = entries_[it->second];
    assert(entry.state != ATTEntry::FREE && entry.phy_tag == phy_tag);
    // LRU
    queues_[entry.state].Remove(it->second);
    queues_[entry.state].PushBack(it->second);

    return make_pair(it->second, entry.mach_base);
  }
}

void AddrTransTable::Setup(Tag phy_tag, Addr mach_base,
    ATTEntry::State state, ATTEntry::SubState sub) {
  assert(tag_index_.count(phy_tag) == 0);
  assert(!queues_[ATTEntry::FREE].Empty());

  int i = queues_[ATTEntry::FREE].PopFront();
  queues_[state].PushBack(i);
  entries_[i].state = state;
  entries_[i].phy_tag = phy_tag;
  entries_[i].mach_base = mach_base;
  entries_[i].sub = sub;

  tag_index_[phy_tag] = i;
}

void AddrTransTable::FreeEntry(int index) {
  ATTEntry& entry = entries_[index];
  assert(entry.state != ATTEntry::FREE);
  tag_index_.erase(entry.phy_tag); 
  queues_[entry.state].Remove(index);
  queues_[ATTEntry::FREE].PushBack(index);
  entry.state = ATTEntry::FREE;
  entry.sub = ATTEntry::REGULAR;
}

void AddrTransTable::CleanEntry(int index) {
  ATTEntry& entry = entries_[index];
  assert(entry.state == ATTEntry::DIRTY && entry.sub == ATTEntry::REGULAR);
  queues_[ATTEntry::DIRTY].Remove(index);
  queues_[ATTEntry::CLEAN].PushBack(index);
  entries_[index].state = ATTEntry::CLEAN;
}

void AddrTransTable::Revoke(Tag phy_tag) {
  unordered_map<Tag, int>::iterator it = tag_index_.find(phy_tag);
  if (it != tag_index_.end()) {
    assert(entries_[it->second].phy_tag == phy_tag);
    FreeEntry(it->second);
  }
}

void AddrTransTable::Reset(int index, Addr mach_base,
    ATTEntry::State state, ATTEntry::SubState sub) {
  ATTEntry& entry = entries_[index];
  entry.mach_base = mach_base;
  queues_[entry.state].Remove(index);
  queues_[state].PushBack(index);
  entry.state = state;
  entry.sub = sub;
}

