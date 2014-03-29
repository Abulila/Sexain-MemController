/*
 * Copyright (c) 2010-2013 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Ron Dreslinski
 *          Ali Saidi
 *          Andreas Hansson
 */

#include "base/random.hh"
#include "mem/simple_mem.hh"

using namespace std;

SimpleMemory::SimpleMemory(const SimpleMemoryParams* p) :
    AbstractMemory(p),
    port(name() + ".port", *this), latency(p->latency),
    latATTLookup(p->lat_att_lookup),
    latATTUpdate(p->lat_att_update), latBlkWriteback(p->lat_blk_writeback),
    latNVMRead(p->lat_nvm_read), latNVMWrite(p->lat_nvm_write),
    isLatATT(p->is_lat_att), latency_var(p->latency_var),
    bandwidth(p->bandwidth), bandwidthNVM(p->bandwidth * 10), isBusy(false),
    retryReq(false), retryResp(false),
    releaseEvent(this), dequeueEvent(this), drainManager(NULL)
{
    latATT = 0;
    checkNumPages = 0;
}

void
SimpleMemory::init()
{
    // allow unconnected memories as this is used in several ruby
    // systems at the moment
    if (port.isConnected()) {
        port.sendRangeChange();
    }
}

void
SimpleMemory::regStats()
{
    using namespace Stats;
    AbstractMemory::regStats();

    totalLatency
        .name(name() + ".totalLatency")
        .desc("Total latency in memory responds");
    sumLatATT
        .name(name() + ".sumLatATT")
        .desc("Additional latency due to ATT in memory responds");
}

Tick
SimpleMemory::recvAtomic(PacketPtr pkt)
{
    assert(latATT == 0);
    access(pkt);

    Tick lat = getLatency();
    if (isLatATT) {
        lat += latATT;
        sumLatATT += latATT;
    }
    latATT = 0;

    return pkt->memInhibitAsserted() ? 0 : lat;
}

void
SimpleMemory::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    assert(latATT == 0);
    functionalAccess(pkt);
    latATT = 0;

    // potentially update the packets in our packet queue as well
    for (auto i = packetQueue.begin(); i != packetQueue.end(); ++i)
        pkt->checkFunctional(i->pkt);

    pkt->popLabel();
}

bool
SimpleMemory::recvTimingReq(PacketPtr pkt)
{
    /// @todo temporary hack to deal with memory corruption issues until
    /// 4-phase transactions are complete
    for (int x = 0; x < pendingDelete.size(); x++)
        delete pendingDelete[x];
    pendingDelete.clear();

    if (pkt->memInhibitAsserted()) {
        // snooper will supply based on copy of packet
        // still target's responsibility to delete packet
        pendingDelete.push_back(pkt);
        return true;
    }

    // we should never get a new request after committing to retry the
    // current one, the bus violates the rule as it simply sends a
    // retry to the next one waiting on the retry list, so simply
    // ignore it
    if (retryReq)
        return false;

    // if we are busy with a read or write, remember that we have to
    // retry
    if (isBusy) {
        retryReq = true;
        return false;
    }

    // @todo someone should pay for this
    pkt->busFirstWordDelay = pkt->busLastWordDelay = 0;

    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory

    // only look at reads and writes when determining if we are busy,
    // and for how long, as it is not clear what to regulate for the
    // other types of commands
    if (pkt->isRead() || pkt->isWrite()) {
        // calculate an appropriate tick to release to not exceed
        // the bandwidth limit
        AddrStatus status = addrController.Probe(localAddr(pkt));
        Tick duration = status.type ? 
                pkt->getSize() * bandwidth : pkt->getSize() * bandwidthNVM;

        if (isLatATT && pkt->isWrite()) {
            if (status.oper == EPOCH) {
                duration += pageTable.block_size() * epochPages * bandwidthNVM;
                duration += blockTable.length() * 16 * bandwidthNVM;
                duration += pageTable.length() * 16 * bandwidthNVM;
                addrController.NewEpoch();
            } else if (!status.type && status.oper == WRBACK) {
                assert(pkt->getSize() == blockTable.block_size());
                duration += pkt->getSize() * bandwidthNVM;
            }
        }

        // only consider ourselves busy if there is any need to wait
        // to avoid extra events being scheduled for (infinitely) fast
        // memories
        if (duration != 0) {
            schedule(releaseEvent, curTick() + duration);
            isBusy = true;
        }
    }

    // go ahead and deal with the packet and put the response in the
    // queue if there is one
    bool needsResponse = pkt->needsResponse();
    Tick lat = recvAtomic(pkt);
    // turn packet around to go back to requester if response expected
    if (needsResponse) {
        // recvAtomic() should already have turned packet into
        // atomic response
        assert(pkt->isResponse());
        // to keep things simple (and in order), we put the packet at
        // the end even if the latency suggests it should be sent
        // before the packet(s) before it
        packetQueue.push_back(DeferredPacket(pkt, curTick() + lat));
        if (!retryResp && !dequeueEvent.scheduled())
            schedule(dequeueEvent, packetQueue.back().tick);
        totalLatency += lat;
    } else {
        pendingDelete.push_back(pkt);
    }

    return true;
}

void
SimpleMemory::release()
{
    assert(isBusy);
    isBusy = false;
    if (retryReq) {
        retryReq = false;
        port.sendRetry();
    }
}

void
SimpleMemory::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();

    retryResp = !port.sendTimingResp(deferred_pkt.pkt);

    if (!retryResp) {
        packetQueue.pop_front();

        // if the queue is not empty, schedule the next dequeue event,
        // otherwise signal that we are drained if we were asked to do so
        if (!packetQueue.empty()) {
            // if there were packets that got in-between then we
            // already have an event scheduled, so use re-schedule
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        } else if (drainManager) {
            drainManager->signalDrainDone();
            drainManager = NULL;
        }
    }
}

Tick
SimpleMemory::getLatency()
{
    Tick lat = latency;
    if (isLatATT) {
        lat += latATTLookup;
        sumLatATT += latATTLookup;
    }
    return lat +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
}

void
SimpleMemory::recvRetry()
{
    assert(retryResp);

    dequeue();
}

BaseSlavePort &
SimpleMemory::getSlavePort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return MemObject::getSlavePort(if_name, idx);
    } else {
        return port;
    }
}

unsigned int
SimpleMemory::drain(DrainManager *dm)
{
    int count = 0;

    // also track our internal queue
    if (!packetQueue.empty()) {
        count += 1;
        drainManager = dm;
    }

    if (count)
        setDrainState(Drainable::Draining);
    else
        setDrainState(Drainable::Drained);
    return count;
}

SimpleMemory::MemoryPort::MemoryPort(const std::string& _name,
                                     SimpleMemory& _memory)
    : SlavePort(_name, &_memory), memory(_memory)
{ }

AddrRangeList
SimpleMemory::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(memory.getAddrRange());
    return ranges;
}

Tick
SimpleMemory::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return memory.recvAtomic(pkt);
}

void
SimpleMemory::MemoryPort::recvFunctional(PacketPtr pkt)
{
    memory.recvFunctional(pkt);
}

bool
SimpleMemory::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return memory.recvTimingReq(pkt);
}

void
SimpleMemory::MemoryPort::recvRetry()
{
    memory.recvRetry();
}

SimpleMemory*
SimpleMemoryParams::create()
{
    return new SimpleMemory(this);
}

void
SimpleMemory::OnDirectWrite(uint64_t phy_tag, uint64_t mach_tag, int bits)
{
    AbstractMemory::OnDirectWrite(phy_tag, mach_tag, bits);
    latATT += latATTUpdate;
}

void
SimpleMemory::OnWriteBack(uint64_t phy_tag, uint64_t mach_tag, int bits)
{
    AbstractMemory::OnWriteBack(phy_tag, mach_tag, bits);
    latATT += latBlkWriteback;
}

void
SimpleMemory::OnOverwrite(uint64_t phy_tag, uint64_t mach_tag, int bits)
{
    AbstractMemory::OnOverwrite(phy_tag, mach_tag, bits);
}

void
SimpleMemory::OnShrink(uint64_t phy_tag, uint64_t mach_tag, int bits)
{
    AbstractMemory::OnShrink(phy_tag, mach_tag, bits);
    latATT += latATTUpdate;
}

void
SimpleMemory::OnEpochEnd(int bits)
{
    checkNumPages += epochPages;
    assert(checkNumPages == numPages.value());
    AbstractMemory::OnEpochEnd(bits);
}

void
SimpleMemory::OnNVMRead(uint64_t mach_addr) {
    latATT += latNVMRead;
}

void
SimpleMemory::OnNVMWrite(uint64_t mach_addr) {
    latATT += latNVMWrite;
}
