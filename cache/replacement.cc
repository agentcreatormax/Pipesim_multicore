#include "cache/replacement.h"

#include "cache/cache_types.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pipesim {
namespace cache {
namespace {

[[maybe_unused]] void CheckCoordinates(std::uint32_t set, std::uint32_t way,
                      std::uint32_t sets, std::uint32_t ways) {
  if (set >= sets || way >= ways) {
    throw std::runtime_error("replacement-policy coordinate out of range");
  }
}

// Tree-PLRU. Implement Reset/BeginCycle/OnHit/OnInsert/
// PeekVictim/SelectVictim/DebugState per SPEC.md/ABI.md.
//
// Worked example (current/next commit discipline only -- the same shape
// as tutorial.cc's ValueRegister::Commit): Commit() below is fully wired.
// Every other method here needs the same current_/planned_ separation, but
// with the actual tree-bit read/update logic, which is not given.
class TreePlru final : public ReplacementPolicy {
 public:
  TreePlru(std::uint32_t sets, std::uint32_t ways)
      : sets_(sets), ways_(ways),
        current_(sets, std::vector<bool>(ways - 1)),
        planned_(sets, std::vector<bool>(ways - 1)) {
    if (!IsPowerOfTwo(ways_)) {
      throw std::runtime_error("Tree-PLRU requires power-of-two ways");
    }
  }

  void Reset() override {
    //Start
    for(uint32_t i=0;i<sets_;i++){
      current_[i].assign(ways_-1,0);
      planned_[i].assign(ways_-1,0);
    }
    begun_=0;
    event_planned_=0;
    //End
  }

  void BeginCycle() override {
    //Start
    if(begun_)throw std::runtime_error("Alredy Done");
    planned_=current_;
    begun_=1;
    event_planned_=0;
    //End
  }

  void OnHit(std::uint32_t set, std::uint32_t way) override {
    //Start
    if(event_planned_)throw std::runtime_error("Multiple");
    event_planned_=1;
    uint32_t node=0;
    uint32_t l=0,r=ways_-1;
    while(l<r) {
      uint32_t mid=l+(r-l)/2;
      if(way<=mid){
        // Accessed left: mark the right subtree as the next LRU direction.
        planned_[set][node]=1;
        node=2*node+1;
        r=mid;
      } 
      else{
        // Accessed right: mark the left subtree as the next LRU direction.
        planned_[set][node]=0;
        node=2*node+2;
        l=mid+1;
      }
    }
    //End
  }

  void OnInsert(std::uint32_t set, std::uint32_t way) override {
    //Start
    OnHit(set, way);
    //End
  }

  int SelectVictim(std::uint32_t set,
                   const std::vector<bool>& eligible) override {
    //Start
    if(event_planned_)throw std::runtime_error("Multiple");
    event_planned_=1;
    return PeekVictim(set,eligible);
    //End
  }

  int PeekVictim(std::uint32_t set,
                 const std::vector<bool>& eligible) const override {
    //Start
    uint32_t node=0;
    uint32_t l=0,r=ways_-1;
    while(l<r){
      uint32_t mid=l+(r-l)/2;
      bool f1=0,f2=0;
      for(uint32_t i=l;i<=mid;i++){
        if(eligible[i]){
          f1=1;break;
        }
      }
      for(uint32_t i=mid+1;i<=r;i++){
        if(eligible[i]){
          f2=1;break;
        }
      }
      const bool prefer_right=current_[set][node];
      if(!prefer_right && f1){
        node=2*node+1;
        r=mid;
      }
      else if(prefer_right && f2){
        node=2*node+2;
        l=mid+1;
      }
      else if(f1){
        node=2*node+1;
        r=mid;
      }
      else if(f2){
        node=2*node+2;
        l=mid+1;
      }
      else{
        return -1;
      }
    }
    return l;
    //End
  }

  std::vector<std::uint32_t> DebugState(std::uint32_t set) const override {
    std::vector<uint32_t>s;
    for(auto x:current_[set]){
      s.push_back(x?1:0);
    }
    return s;
  }

  // Worked example: the general current/next commit discipline (compare
  // tutorial.cc's ValueRegister::Commit). Every other method above must
  // maintain the same current_/planned_ separation.
  void Commit() override {
    if (!begun_) throw std::runtime_error("Tree-PLRU Commit without begin");
    current_ = planned_;
    begun_ = false;
    event_planned_ = false;
  }

 private:
  std::uint32_t sets_;
  std::uint32_t ways_;
  std::vector<std::vector<bool>> current_;
  std::vector<std::vector<bool>> planned_;
  bool begun_ = false;
  bool event_planned_ = false;
};

//SRRIP. Implement Reset/BeginCycle/OnHit/OnInsert/
// PeekVictim/SelectVictim/DebugState per SPEC.md/ABI.md (2-bit RRPV,
// init 3, hit 0, insertion 2, saturating aging, lowest-way tie-break).
class Srrip final : public ReplacementPolicy {
 public:
  Srrip(std::uint32_t sets, std::uint32_t ways)
      : sets_(sets), ways_(ways),
        current_(sets, std::vector<std::uint8_t>(ways, 3)),
        planned_(sets, std::vector<std::uint8_t>(ways, 3)) {}

  void Reset() override {
    for(uint32_t i=0;i<sets_;i++){
      current_[i].assign(ways_,3);
      planned_[i].assign(ways_,3);
    }
    begun_=0;
    event_planned_=0;
  }

  void BeginCycle() override {
    if(begun_)throw std::runtime_error("Alredy Done");
    planned_=current_;
    begun_=1;
    event_planned_=0;
  }

  void OnHit(std::uint32_t set, std::uint32_t way) override {
    if(event_planned_)throw std::runtime_error("Multiple");
    event_planned_=1;
    planned_[set][way]=0;
  }

  void OnInsert(std::uint32_t set, std::uint32_t way) override {
    if(event_planned_)throw std::runtime_error("Multiple");
    event_planned_=1;
    planned_[set][way]=2;
  }

  int SelectVictim(std::uint32_t set,
                   const std::vector<bool>& eligible) override {
    if(event_planned_)throw std::runtime_error("Multiple");
    event_planned_=1;
    uint32_t maxi=0;
    for(uint32_t i=0;i<ways_;i++){
      if(eligible[i] && planned_[set][i]>maxi){
        maxi=planned_[set][i];
      }
    }
    if(3-maxi>0){
      for(uint32_t i=0;i<ways_;i++){
        if(eligible[i])planned_[set][i]+=3-maxi;
      }
    }
    for(uint32_t i=0;i<ways_;i++){
      if(eligible[i] && planned_[set][i]==3)return i;
    }
    throw std::runtime_error("Not possible");
  }

  int PeekVictim(std::uint32_t set,
                 const std::vector<bool>& eligible) const override {
    uint32_t maxi=0;
    for(uint32_t i=0;i<ways_;i++){
      if(eligible[i] && current_[set][i]>maxi){
        maxi=current_[set][i];
      }
    }
    for(uint32_t i=0;i<ways_;i++){
      if(eligible[i] && current_[set][i]==maxi)return i;
    }
    throw std::runtime_error("Not possible");
  }

  std::vector<std::uint32_t> DebugState(std::uint32_t set) const override {
    std::vector<uint32_t>s;
    for(auto x:current_[set]){
      s.push_back(x);
    }
    return s;
  }

  void Commit() override {
    if (!begun_) throw std::runtime_error("SRRIP Commit without begin");
    current_ = planned_;
    begun_ = false;
    event_planned_ = false;
  }

 private:
  std::uint32_t sets_;
  std::uint32_t ways_;
  std::vector<std::vector<std::uint8_t>> current_;
  std::vector<std::vector<std::uint8_t>> planned_;
  bool begun_ = false;
  bool event_planned_ = false;
};

}  // namespace

// return the requested concrete policy for the supplied
// sets/ways, or fail on an impossible enumerator. Worked example below.
std::unique_ptr<ReplacementPolicy> MakeReplacementPolicy(
    ReplacementKind kind, std::uint32_t sets, std::uint32_t ways) {
  switch (kind) {
    case ReplacementKind::kTreePlru:
      return std::unique_ptr<ReplacementPolicy>(new TreePlru(sets, ways));
    case ReplacementKind::kSrrip:
      return std::unique_ptr<ReplacementPolicy>(new Srrip(sets, ways));
  }
  throw std::runtime_error("unknown replacement policy");
}

}  // namespace cache
}  // namespace pipesim
