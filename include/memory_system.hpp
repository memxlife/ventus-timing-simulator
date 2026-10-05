// Copyright (c) 2026 memxlife
// Licensed under Mulan PSL v2; see LICENSE and NOTICE.
// Timing rules adapted from Ventus and the SiFive inclusive cache.
// Copyright (c) 2021-2022 International Innovation Center of Tsinghua University, Shanghai
// Copyright 2019 SiFive, Inc.
// Upstream terms: Mulan PSL v2 and Apache-2.0; see licenses/ and NOTICE.
#pragma once
// Timing-only port of read_system.py + store_system.py. Payload-free interfaces.
#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
namespace ventus_timing {
struct MemRequest {int opcode=0,instrId=0;uint32_t address=0,activeMask=0;std::array<uint32_t,32> lane_addresses{};};
struct ICacheRequest {uint32_t address=0;int entry=0;};
using ICacheResponse=ICacheRequest;
struct Response {int index=0;uint32_t mask=0;};
inline bool operator==(const Response&a,const Response&b){return a.index==b.index&&a.mask==b.mask;}
struct Outputs {bool request_ready=false,icache_ready=true;std::optional<Response> response;std::optional<uint32_t> icache_fill;std::optional<ICacheResponse> icache_response;};
struct MemoryEvent {int twice_cycle=0;std::string kind;uint32_t address=0;std::array<uint32_t,4> mask{};};
inline void require(bool x,const char*s){if(!x)throw std::runtime_error(s);}
template<class T> std::vector<T> take(std::map<int,std::vector<T>>&m,int t){auto p=m.find(t);if(p==m.end())return {};auto v=std::move(p->second);m.erase(p);return v;}
struct Packet {uint32_t address=0;int entry=0,wshr=0;bool instruction=false;MemRequest core;std::array<uint32_t,4> mask{};};
struct Slot {Packet packet;bool a_pending=true,d_ready=false;};
inline int select_slot(const std::map<int,Slot>&s,int last,int t,int busy){int first=-1,after=-1;for(auto& [i,x]:s)if(x.a_pending||(x.d_ready&&t>=busy)){if(first<0)first=i;if(i>last&&after<0)after=i;}return after<0?first:after;}
inline int free_slot(const std::map<int,Slot>&s){for(int i=0;i<32;++i)if(!s.count(i))return i;throw std::runtime_error("L2 capacity outside scope");}
struct DCacheRead {
    struct Entry {uint32_t address;std::deque<Response> targets;};
    std::optional<MemRequest> core;
    std::optional<Response> stage,missout;
    std::deque<Response> responses;
    std::deque<Packet> memory_responses,memory_requests;
    std::optional<Packet> memory_stage;
    std::map<int,Entry> entries;std::set<uint32_t> tags;bool releasing=false;
    std::optional<Response> response()const{return responses.empty()?std::nullopt:std::optional<Response>(responses.front());}
    bool empty()const{return !core&&!stage&&!missout&&responses.empty()&&memory_responses.empty()&&memory_requests.empty()&&!memory_stage&&entries.empty();}
    Outputs step(int t,const std::optional<MemRequest>&request,const std::optional<Packet>&fill_offer,bool rsp_ready,bool mem_ready){
        if(request){require(request->opcode==0,"read-only opcode");require(request->address%128==0&&request->activeMask!=0,"invalid line/mask");}
        Outputs out;out.response=response();
        const bool response_fire=out.response&&rsp_ready,room=responses.size()<32;
        const bool stage_depart=stage&&room,stage_room=!stage||stage_depart;
        const bool miss_depart=missout&&stage_room,miss_room=!missout||miss_depart;
        const auto fill=memory_responses.empty()?std::nullopt:std::optional<Packet>(memory_responses.front());
        std::optional<Response> target;
        if(fill){auto f=entries.find(fill->entry);require(f!=entries.end(),"unknown L1 response entry");if(!f->second.targets.empty())target=f->second.targets.front();}
        bool full=false;
        if(request)for(auto& [i,e]:entries){(void)i;if(e.address==request->address&&e.targets.size()>=2)full=true;}
        const bool depart=core&&!missout&&stage_room&&memory_requests.size()<8;
        const bool same=core&&request&&core->address==request->address&&!tags.count(core->address);
        out.request_ready=t>=258&&(!core||depart)&&!same&&!releasing&&!fill&&!full;
        const bool memory_fire=memory_stage&&mem_ready;
        const bool memory_pop=!memory_requests.empty()&&mem_ready&&!missout;
        const bool memory_rsp_ready=memory_responses.size()<2;
        if(response_fire)responses.pop_front();
        if(stage_depart){responses.push_back(*stage);stage.reset();}
        if(miss_depart){stage=missout;missout.reset();}
        if(depart){auto r=*core;core.reset();if(tags.count(r.address)){require(!stage,"hit/fill collision");stage=Response{r.instrId,r.activeMask};}
            else {int idx=-1;for(auto& [i,e]:entries)if(e.address==r.address)idx=i;
                if(idx<0){for(int i=0;i<4;++i)if(!entries.count(i)){idx=i;break;}require(idx>=0,"L1 capacity");entries.emplace(idx,Entry{r.address,{}});Packet p;p.entry=idx;p.address=r.address;memory_requests.push_back(p);}
                auto& e=entries.at(idx);require(e.targets.size()<2,"full L1 subentry");e.targets.push_back(Response{r.instrId,r.activeMask});}}
        if(fill){auto& e=entries.at(fill->entry);if(target&&miss_room){missout=e.targets.front();e.targets.pop_front();tags.insert(e.address);}if(!target){memory_responses.pop_front();entries.erase(fill->entry);releasing=false;}else releasing=true;}
        if(memory_fire)memory_stage.reset();
        if(memory_pop){memory_stage=memory_requests.front();memory_requests.pop_front();}
        if(request&&out.request_ready)core=request;
        if(fill_offer){require(memory_rsp_ready,"L1 return backpressure outside scope");memory_responses.push_back(*fill_offer);}
        return out;
    }
};
struct ColdReadMemory {
    std::deque<Packet> cluster;std::map<int,Slot> slots;
    std::map<int,std::vector<Packet>> allocate,returns;
    std::map<int,std::vector<int>> ready;
    int last=-1,busy=-1;std::set<uint32_t> accepted_lines,tags;std::deque<Packet>hits;std::map<int,std::vector<Packet>>hit_due;
    bool empty()const{return cluster.empty()&&slots.empty()&&allocate.empty()&&returns.empty()&&ready.empty()&&hits.empty()&&hit_due.empty();}
    std::vector<Packet> responses(int t)const{auto p=returns.find(t);return p==returns.end()?std::vector<Packet>{}:p->second;}
    void step(int t,const std::optional<ICacheRequest>&ir,const std::optional<Packet>&dr){
        const bool room=cluster.size()<2;
        const int selected=select_slot(slots,last,t,busy);
        if(selected>=0){auto&s=slots.at(selected);last=selected;if(s.a_pending){s.a_pending=false;ready[t+8].push_back(selected);}else{returns[t+1].push_back(s.packet);tags.insert(s.packet.address);busy=t+2;slots.erase(selected);}}
        if(!hits.empty()&&t>=busy){returns[t+1].push_back(hits.front());hits.pop_front();busy=t+2;}
        for(auto&r:take(hit_due,t))hits.push_back(r);
        (void)take(returns,t);
        for(int i:take(ready,t))slots.at(i).d_ready=true;
        for(auto&r:take(allocate,t)){for(auto&[i,s]:slots){(void)i;require(s.packet.address!=r.address,"L2 merge outside scope");}slots.emplace(free_slot(slots),Slot{r,true,false});}
        if(!cluster.empty()&&t>=67){auto r=cluster.front();cluster.pop_front();accepted_lines.insert(r.address);if(tags.count(r.address)){require(hits.empty()&&hit_due.empty(),"directory-hit queue contention outside scope");hit_due[t+1].push_back(r);}else allocate[t+1].push_back(r);}
        if(room){if(ir){Packet p;p.address=ir->address;p.entry=ir->entry;p.instruction=true;cluster.push_back(p);}else if(dr)cluster.push_back(*dr);}
    }
};
struct ReadSystem {
    DCacheRead cache;ColdReadMemory memory;std::optional<uint32_t> instruction_fill;std::optional<ICacheResponse> instruction_response;
    Outputs step(int t,const std::optional<MemRequest>&request,const std::optional<ICacheRequest>&ir,bool ready){
        auto incoming=memory.responses(t);std::optional<Packet> d,i;
        for(auto&r:incoming){if(r.instruction){require(!i,"ICache response collision");i=r;}else{require(!d,"DCache response collision");d=r;}}
        const bool room=memory.cluster.size()<2;
        const auto pending=cache.memory_stage;
        auto out=cache.step(t,request,d,ready,room&&!ir);
        memory.step(t,ir,pending);out.icache_ready=room;out.icache_fill=instruction_fill;out.icache_response=instruction_response;
        instruction_fill=i?std::optional<uint32_t>(i->address):std::nullopt;instruction_response=i?std::optional<ICacheResponse>(ICacheResponse{i->address,i->entry}):std::nullopt;return out;
    }
};
struct StoreSystem {
    std::optional<Packet> core,stage;std::deque<Packet> memory,cluster;std::deque<Response> responses;
    std::map<int,Packet>wshr;std::map<int,Slot>slots;
    std::map<int,std::vector<Packet>>allocate;
    std::map<int,std::vector<int>>ready,acks;
    std::map<int,Packet>forward;
    std::map<int,std::vector<std::string>>tail;
    std::map<int,std::vector<ICacheResponse>>instruction_returns;
    std::set<uint32_t>resident;std::vector<uint32_t>pattern;
    int writes=0,last=-1,d_busy=-1;bool busy=false,wait=false,sent=false;
    std::vector<MemoryEvent>events;
    explicit StoreSystem(const std::set<uint32_t>&r):resident(r){}
    std::optional<Response>response()const{return responses.empty()?std::nullopt:std::optional<Response>(responses.front());}
    void emit(int twice,const char*kind,const Packet&r){events.push_back(MemoryEvent{twice,kind,r.address,r.mask});}
    void schedule_tail(int offer){int count=0;for(int pass=0;pass<2;++pass){int accept=offer+3;count=(count+1)%2048;int terminal=accept+(1023-count+2048)%2048+1;count=1024;tail[accept].push_back("busy");tail[terminal+3].push_back("unblock");tail[terminal+4].push_back("clear");offer=terminal+5;}}
    Outputs step(int t,const std::optional<MemRequest>&request,const std::optional<ICacheRequest>&ir,bool rsp_ready){
        if(request)require(request->opcode==1||request->opcode==3,"store epoch opcode");
        Outputs out;out.response=response();out.icache_ready=cluster.size()<2;
        auto fills=take(instruction_returns,t);require(fills.size()<2,"instruction response collision");if(!fills.empty()){out.icache_response=fills.front();out.icache_fill=fills.front().address;}
        const bool response_fire=out.response&&rsp_ready,room=responses.size()<32;
        const auto l2=!cluster.empty()&&!busy?std::optional<Packet>(cluster.front()):std::nullopt;
        const bool stage_out=stage&&out.icache_ready&&!ir;
        bool same=false;if(!memory.empty())for(auto&[idx,r]:wshr){(void)idx;if(r.address==memory.front().address)same=true;}
        const bool mem_out=!memory.empty()&&wshr.size()<4&&cluster.size()<2&&!ir&&room&&!same;
        const bool flushing=core&&core->core.opcode==3;
        const bool core_out=core&&memory.size()<8&&(!flushing||(!wait&&wshr.empty()));
        out.request_ready=!core||core_out;
        if(flushing&&wshr.empty()&&!sent){require(slots.empty()&&allocate.empty(),"invalidate concurrent L2 traffic outside scope");sent=true;if(wait)schedule_tail(t);}
        const auto oldstage=stage;
        if(l2){cluster.pop_front();allocate[t+1].push_back(*l2);}
        if(stage_out){cluster.push_back(*oldstage);stage.reset();}
        if(ir&&out.icache_ready){Packet p;p.address=ir->address;p.entry=ir->entry;p.instruction=true;cluster.push_back(p);}
        if(response_fire)responses.pop_front();
        if(mem_out){auto r=memory.front();memory.pop_front();int idx=0;while(wshr.count(idx))++idx;r.wshr=idx;wshr.emplace(idx,r);stage=r;responses.push_back(Response{r.core.instrId,r.core.activeMask});}
        if(core_out){if(!flushing)memory.push_back(*core);core.reset();}
        if(request&&out.request_ready){Packet r;r.address=request->address;r.core=*request;
            if(request->opcode==3)wait=true;
            else{require(!resident.count(r.address),"store hit on retained read line");if(writes<4)pattern.push_back(r.address);else require(r.address==pattern.at(writes%4),"store line-order scope");if(writes==3)require(std::set<uint32_t>(pattern.begin(),pattern.end()).size()==4,"four distinct store lines");++writes;
                for(int lane=0;lane<32;++lane)if((request->activeMask>>lane)&1U){const int offset=request->lane_addresses[lane]%128;require(offset%4==0,"unaligned store word");for(int k=0;k<4;++k)r.mask[(offset+k)/32]|=uint32_t(1)<<((offset+k)%32);}}
            core=r;}
        for(int idx:take(acks,t))require(wshr.erase(idx)==1,"unknown write acknowledge");
        const int selected=select_slot(slots,last,t,d_busy);
        auto f=forward.find(t);const bool forwarding=f!=forward.end();
        if(forwarding){emit(t*2,"source_a_write_accept",f->second);emit(t*2+3,"physical_write",f->second);forward.erase(f);}
        if(selected>=0){auto&s=slots.at(selected);last=selected;const auto r=s.packet;
            if(s.a_pending){if(!forwarding){emit(t*2,"source_a_get_accept",r);s.a_pending=false;ready[t+8].push_back(selected);}}
            else{if(r.instruction){emit(t*2,"source_d_read_accept",r);instruction_returns[t+2].push_back(ICacheResponse{r.address,r.entry});}else{emit(t*2,"source_d_write_accept",r);forward[t+1]=r;acks[t+2].push_back(r.wshr);}d_busy=t+2;slots.erase(selected);}}
        for(int i:take(ready,t))slots.at(i).d_ready=true;
        for(auto&r:take(allocate,t)){for(auto&[i,s]:slots){(void)i;require(s.packet.address!=r.address,"L2 merged store");}slots.emplace(free_slot(slots),Slot{r,true,false});}
        for(auto&e:take(tail,t)){if(e=="busy")busy=true;else if(e=="unblock")busy=false;else if(e=="clear"){wait=false;sent=false;}}
        return out;
    }
};
class MemorySystem {
public:
    ReadSystem read;std::unique_ptr<StoreSystem> store;
    std::optional<Response>peek(int /*cycle*/,const std::optional<ICacheRequest>& /*ir*/=std::nullopt)const{return store?store->response():read.cache.response();}
    // Caller must also prove that frontend/LSU/coalescer have no work or new
    // offer before this edge. This method certifies only memory quiescence.
    int next_idle_wakeup(int t)const {
        if(!store)return t+1;
        const auto& s=*store;
        if(!s.busy||!s.responses.empty()||!s.slots.empty()||!s.allocate.empty()||!s.ready.empty()||!s.acks.empty()||!s.forward.empty()||!s.instruction_returns.empty())return t+1;
        if(s.stage&&s.cluster.size()<2)return t+1;
        if(!s.memory.empty()&&s.wshr.size()<4&&s.cluster.size()<2)return t+1;
        if(s.core){bool inv=s.core->core.opcode==3;if(s.memory.size()<8&&(!inv||(!s.wait&&s.wshr.empty())))return t+1;if(inv&&s.wshr.empty()&&!s.sent)return t+1;}
        auto next=s.tail.upper_bound(t);return next==s.tail.end()?t+1:next->first;
    }
    Outputs step(int t,const std::optional<MemRequest>&request,const std::optional<ICacheRequest>&ir,bool ready){
        if(!store&&request&&(request->opcode==1||request->opcode==3)){
            require(read.cache.empty()&&read.memory.empty()&&!read.instruction_fill,"read/store epoch overlap");
            auto retained=read.cache.tags;retained.insert(read.memory.accepted_lines.begin(),read.memory.accepted_lines.end());store=std::make_unique<StoreSystem>(retained);}
        return store?store->step(t,request,ir,ready):read.step(t,request,ir,ready);
    }
};
}
