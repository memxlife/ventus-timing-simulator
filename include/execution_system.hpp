// Copyright (c) 2026 memxlife
// Licensed under Mulan PSL v2; see LICENSE and NOTICE.
// Timing rules adapted from Ventus.
// Copyright (c) 2021-2022 International Innovation Center of Tsinghua University, Shanghai
#pragma once
#include "memory_system.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
namespace ventus_timing {
enum Kind{ALU,FMA,LOAD,STORE,END,BAD};
struct Instruction{uint32_t pc=0,word=0;Kind kind=ALU;bool scalar=false;std::array<int,3>src{-1,-1,-1};int dst=-1;std::array<uint32_t,32>addresses{};};
inline int sign_extend(uint32_t x,int n){return int(x)-(x&(uint32_t(1)<<(n-1))?int(uint32_t(1)<<n):0);}
inline Instruction decode(uint32_t pc,uint32_t w){Instruction q;q.pc=pc;q.word=w;int op=w&127,d=(w>>7)&31,a=(w>>15)&31,b=(w>>20)&31;
 if(w==0x400b){q.kind=END;q.scalar=true;}else if(w==0x13){q.src[0]=0;q.dst=0;q.scalar=true;}else if(op==0x37){q.dst=d;q.scalar=true;}
 else if(op==0x7b){bool store=((w>>12)&7)==6;require(store||((w>>12)&7)==2,"unsupported memory word");q.kind=store?STORE:LOAD;q.src[0]=32+a;q.dst=store?-1:32+d;if(store)q.src[2]=32+b;}
 else if(op==0x57){int fn=w>>26,mode=(w>>12)&7;q.dst=32+d;if((fn==20&&mode==2)||(fn==23&&mode==3)){}else if((fn==40||fn==37||fn==9)&&mode==3)q.src[1]=32+b;else if(fn==0&&(mode==0||mode==4)){q.src[0]=(mode==0?32:0)+a;q.src[1]=32+b;}else if(fn==44&&mode==1){q.kind=FMA;q.src={32+a,32+b,32+d};}else throw std::runtime_error("unsupported vector word");}
 else throw std::runtime_error("unsupported word");return q;}
inline void addresses(std::vector<Instruction>&program){std::array<std::optional<uint32_t>,32>x{};std::array<std::optional<std::array<uint32_t,32>>,32>v{};x[0]=0;
 for(auto&q:program){uint32_t w=q.word;int op=w&127,d=(w>>7)&31,a=(w>>15)&31,b=(w>>20)&31;if(q.kind==END)break;
 if(q.kind==LOAD||q.kind==STORE){require(v[a].has_value(),"unknown address value");int offset=sign_extend(q.kind==STORE?((w>>25)<<5)|d:w>>20,12);for(int i=0;i<32;++i){q.addresses[i]=(*v[a])[i]+uint32_t(offset);require(q.addresses[i]%4==0,"unaligned word");}if(q.kind==LOAD)v[d].reset();}
 else if(op==0x37)x[d]=w&0xfffff000;else if(w==0x13){}else if(op==0x57){int fn=w>>26,mode=(w>>12)&7;std::optional<std::array<uint32_t,32>>z;
 if(fn==20||fn==23){z.emplace();for(int i=0;i<32;++i)(*z)[i]=fn==20?i:uint32_t(sign_extend(a,5));}
 else if((fn==40||fn==37||fn==9)&&v[b]){z.emplace();for(int i=0;i<32;++i)(*z)[i]=fn==40?(*v[b])[i]>>a:fn==37?(*v[b])[i]<<a:(*v[b])[i]&uint32_t(sign_extend(a,5));}
 else if(fn==0&&v[b]&&((mode==0&&v[a])||(mode==4&&x[a]))){z.emplace();for(int i=0;i<32;++i)(*z)[i]=(*v[b])[i]+(mode==0?(*v[a])[i]:*x[a]);}v[d]=z;}}
}
struct ExecEvent{int cycle;std::string kind;uint32_t pc;int order=-1;};
struct Handler{bool positive;int state=0,count=0;void step(bool req){if(state==0&&req)state=1;else if(state==1)state=2;else if(state==2){if(!positive||count>0)state=3;++count;}}};
struct Launch{bool wgram=false,av=false,hd=false,ad=false,jf=false,finished=false,accepted=false;int phase=0,split=0,count=0;std::array<Handler,3>h{{{false},{true},{true}}};
 bool step(int tick,int first_offer){bool host=!accepted&&count==16&&tick>=first_offer;if(count<16)++count;bool read=wgram&&!av,alloc=av&&phase==0,request=phase==3,join=hd&&ad&&!jf&&std::all_of(h.begin(),h.end(),[](auto&a){return a.state==3;}),warp=split>0,oldjf=jf;
 if(host){wgram=true;accepted=true;}if(read){wgram=false;av=true;}if(alloc){av=false;phase=1;}else if(phase==1)phase=2;else if(phase==2)phase=3;else if(request){phase=0;hd=ad=true;}for(auto&a:h)a.step(request);if(join)jf=true;if(oldjf&&split==0)split=1;if(warp){split=0;jf=false;finished=true;}return warp;}};
struct Fetch{struct Batch{std::array<int,2>q;int mask=3;};struct Pipe{bool valid=false,hit=false;uint32_t pc=0;};
 const std::vector<Instruction>&p;std::map<uint32_t,int>lookup;std::deque<Batch>fifo;Batch batch{{-1,-1},0};Pipe r1,r2;bool active=false,miss2=false,miss3=false,ov2=false,ov3=false,rsp=false,room=false;uint32_t pc=0;std::set<uint32_t>tags;std::optional<Batch>incoming;
 explicit Fetch(const std::vector<Instruction>&v):p(v){for(int i=0;i<int(v.size());++i)lookup[v[i].pc]=i;}
 int head()const{if(!batch.mask)return batch.q[0];return batch.q[__builtin_ctz(unsigned(batch.mask))];}bool valid()const{return batch.mask!=0;}
 std::optional<uint32_t>miss_request()const{return r1.valid&&!r1.hit?std::optional<uint32_t>(r1.pc&~127U):std::nullopt;}
 void begin(){rsp=r2.valid&&!ov2;room=fifo.size()<2;incoming.reset();if(rsp&&!miss2){Batch b;for(int i=0;i<2;++i){auto it=lookup.find(r2.pc+4*i);b.q[i]=it==lookup.end()?-1:it->second;}incoming=b;}}
 void advance(bool output_ready,bool consumed,bool launch,const std::optional<uint32_t>&fill){bool flush=rsp&&(miss2||!room),req=active&&room,m1=r1.valid&&!r1.hit,o1=(miss2&&!ov2)||(miss3&&!ov3);Pipe nr1{req&&!flush,bool(tags.count(pc>>7)),pc},nr2{r1.valid&&!flush,false,r1.pc};if(flush)pc=r2.pc;else if(req)pc+=8;if(launch){pc=p.front().pc;active=true;}if(fill)tags.insert(*fill>>7);r1=nr1;r2=nr2;miss3=miss2;miss2=m1&&!flush;ov3=ov2;ov2=o1;
 int remain=batch.mask&(batch.mask-1);bool pop=!remain&&output_ready&&!fifo.empty(),accept=incoming&&fifo.size()<2;if(consumed)batch.mask=remain;if(pop){batch=fifo.front();fifo.pop_front();}if(accept)fifo.push_back(*incoming);}
};
struct ICacheMSHR{std::array<uint32_t,4>addr{};std::array<std::set<int>,4>used;std::array<bool,4>sent{};std::array<int,2>response_slots;int read=0,busy=-1;
 explicit ICacheMSHR(std::array<int,2>initial):response_slots(initial){}
 std::optional<ICacheRequest>peek()const{for(int i=0;i<4;++i)if(!used[i].empty()&&!sent[i])return ICacheRequest{addr[i],i};return {};}
 void step(const std::optional<uint32_t>&miss,bool memready,const std::optional<ICacheResponse>&fill){auto offer=peek();int response=busy;if(fill){require(busy<0,"ICache fill while draining outside scope");response=fill->entry;require(!used[response].empty()&&addr[response]==fill->address,"unknown ICachefill");response_slots[read]=response;}int selected=response>=0?response:response_slots[read]&3;int sub=-1,matching=-1,free=-1;for(int j=0;j<4;++j)if(!used[selected].count(j)){sub=j;break;}for(int i=0;i<4;++i){if(used[i].empty()&&free<0)free=i;if(miss&&!used[i].empty()&&addr[i]==*miss)matching=i;}
 bool conflict=miss&&response>=0&&addr[response]==*miss,ready=!conflict&&(matching<0?free>=0:sub>=0),req=miss&&ready;int idx=matching<0?free:matching,slot=matching<0?0:sub;
 if(response>=0){require(!used[response].empty(),"empty ICache response");bool last=used[response].size()==1;used[response].erase(used[response].begin());busy=last?-1:response;if(last)sent[response]=false;}
 if(req){used[idx].insert(slot);if(matching<0)addr[idx]=*miss;}if(offer&&memready)sent[offer->entry]=true;if(fill)read^=1;}
};
struct Arithmetic{std::array<std::vector<int>,3>s{{{-1},{-1},{-1,-1,-1,-1,-1}}};struct Signals{std::vector<int>commits;std::array<std::vector<bool>,3>ready;};
 static int producer(const Instruction&q){return q.scalar?0:q.kind==FMA?2:1;}
 Signals signals()const{Signals o;std::array<bool,3>win{s[0].back()>=0,s[1].back()>=0,s[1].back()<0&&s[2].back()>=0};for(int k=0;k<3;++k){if(win[k])o.commits.push_back(s[k].back());o.ready[k].resize(s[k].size());bool r=win[k];for(int i=int(s[k].size())-1;i>=0;--i){r=s[k][i]<0||r;o.ready[k][i]=r;}}return o;}
 void advance(const Signals&o,int input,const std::vector<Instruction>&p){int selected=input<0?-1:producer(p[input]);if(input>=0)require(o.ready[selected][0],"blocked arithmeticinput");for(int k=0;k<3;++k){auto old=s[k];for(int i=0;i<int(old.size());++i)if(o.ready[k][i])s[k][i]=i==0?(k==selected?input:-1):old[i-1];}}
 bool empty()const{for(auto&v:s)for(int x:v)if(x>=0)return false;return true;}
};
struct Frontend{const std::vector<Instruction>&p;Fetch fetch;Arithmetic arithmetic;std::set<int>pending,ready;std::vector<int>returns;std::array<std::array<int,4>,2>last{};int state=0,current=-1;bool ended=false;std::vector<ExecEvent>events;
 explicit Frontend(const std::vector<Instruction>&v):p(v),fetch(v){}
 int request()const{return !ended&&state==2&&current>=0&&(p[current].kind==LOAD||p[current].kind==STORE)?current:-1;}
 bool result_ready(int tag)const{if(tag<0)return false;if(p[tag].kind==STORE)return true;require(p[tag].kind==LOAD,"unexpectedresulttag");return p[tag].scalar?arithmetic.s[0].back()<0:arithmetic.s[1].back()<0&&arithmetic.s[2].back()<0;}
 struct Out{bool request_fire=false,result_fire=false,end_fire=false;};
 Out step(int t,bool lsu_ready,bool result_valid,int tag,bool launch,const std::optional<uint32_t>&fill){if(!ended)fetch.begin();auto ar=arithmetic.signals();Out out;out.result_fire=result_valid&&result_ready(tag);if(out.result_fire){if(p[tag].kind==LOAD)ar.commits.push_back(tag);else events.push_back({t,"lsu_store_complete",p[tag].pc});}
 int candidate=fetch.head();bool hazard=false;if(candidate>=0){auto&q=p[candidate];for(int x:q.src)if(x>0&&pending.count(x))hazard=true;if(q.dst>0&&pending.count(q.dst))hazard=true;}
 bool output_ready=!ended&&state==0&&!hazard,enter=output_ready&&candidate>=0&&fetch.valid(),unit=true;if(current>=0){auto&q=p[current];if(q.kind==LOAD||q.kind==STORE)unit=lsu_ready;else if(q.dst>=0)unit=ar.ready[Arithmetic::producer(q)][0];}bool issue=!ended&&state==2&&unit;out.request_fire=issue&&(p[current].kind==LOAD||p[current].kind==STORE);out.end_fire=issue&&p[current].kind==END;
 int input=issue&&p[current].dst>=0&&p[current].kind!=LOAD?current:-1;std::array<int,3>src{-1,-1,-1};if(enter)src=p[candidate].src;else if(state==1)src=p[current].src;std::vector<int>grants;
 for(int typ=0;typ<2;++typ)for(int bank=0;bank<4;++bank){std::vector<int>ids;int cu=(enter?p[candidate].scalar:current>=0&&p[current].scalar)?1:0;for(int k=0;k<3;++k)if(src[k]>=0&&(enter||!ready.count(k))&&src[k]/32==typ&&src[k]%4==bank)ids.push_back(4*cu+k);if(!ids.empty()){int chosen=-1;for(int v:ids)if(v>last[typ][bank]){chosen=v;break;}if(chosen<0)chosen=ids.front();last[typ][bank]=chosen;grants.push_back(chosen%4);}}
 if(state==1){bool complete=true;for(int k=0;k<3;++k)if(p[current].src[k]>=0&&!ready.count(k))complete=false;if(complete)state=2;}
 for(int k:returns)if(current>=0&&!ready.count(k)&&p[current].src[k]>=0){events.push_back({t,"collector_operand_return",p[current].pc,k});ready.insert(k);}returns=grants;
 if(issue){events.push_back({t,"collector_issue",p[current].pc});if(p[current].kind==END)ended=true;state=0;ready.clear();}
 if(enter){current=candidate;ready.clear();state=std::any_of(p[current].src.begin(),p[current].src.end(),[](int x){return x>=0;})?1:2;if(p[current].dst>0)pending.insert(p[current].dst);events.push_back({t,"collector_accept",p[current].pc});}
 arithmetic.advance(ar,input,p);if(!ended)fetch.advance(output_ready,enter,launch,fill);
 for(int c:ar.commits){if(p[c].dst>=0)pending.erase(p[c].dst);events.push_back({t,p[c].scalar?"writeback_x":"writeback_v",p[c].pc});}return out;}
};
struct Coalescer{int state=0;std::set<int>used;std::array<uint32_t,8>mask{};std::array<int,8>tag{-1,-1,-1,-1,-1,-1,-1,-1};int pending=-1;uint32_t pendingmask=0;
 struct Peek{int selected=0,tag=-1;bool valid=false,reserve_ready=false,response_ready=false;};
 Peek peek()const{int selected=0;bool complete=false;for(int i:used)if(mask[i]==0){selected=i;complete=true;break;}return {selected,tag[selected],complete&&state==2,state==0&&used.size()<8,state==0};}
 int step(int reservation,uint32_t rmask,const std::optional<Response>&response,bool output_ready){std::set<int>complete;for(int i:used)if(mask[i]==0)complete.insert(i);int free=0;while(free<8&&used.count(free))++free;if(free==8)free=0;int selected=complete.empty()?0:*complete.begin();bool add=reservation>=0&&state==0&&used.size()<8,ret=response&&state==0,fire=!complete.empty()&&state==2&&output_ready;int next;
 if(state==0){if(ret)next=add?1:output_ready&&mask[response->index]==response->mask?2:0;else next=!complete.empty()&&output_ready?2:0;}else if(state==2){auto c=complete;c.erase(free);next=output_ready&&!c.empty()?2:0;}else next=output_ready&&!complete.empty()?2:0;
 if(state==0){if(ret){mask[response->index]&=~response->mask;if(add){pending=reservation;pendingmask=rmask;}}else if(add){used.insert(free);tag[free]=reservation;mask[free]=rmask;}}
 else if(state==1){used.insert(free);tag[free]=pending;mask[free]=pendingmask;pending=-1;}else if(fire)used.erase(selected);state=next;return add?free:0;}
};
struct LSU{const std::vector<Instruction>&p;int fifo=-1,phase=0,current=-1,entry=0,outstanding=0;uint32_t remaining=0;bool flushing=false,flush_pending=false;
 explicit LSU(const std::vector<Instruction>&v):p(v){}
 struct Peek{bool ready=false,flush=false,dequeue=false;int reserve=-1;uint32_t mask=0;std::optional<MemRequest>cache;};
 Peek peek()const{Peek out;out.flush=phase==0&&flush_pending;out.dequeue=phase==0&&!flush_pending&&fifo>=0;out.ready=outstanding<4&&(fifo<0||out.dequeue);if(phase==1){out.reserve=current;out.mask=remaining;}if(phase==2){MemRequest r;r.instrId=entry;if(flushing)r.opcode=3;else{require(remaining!=0,"empty lane set");int lane=__builtin_ctz(remaining);r.address=p[current].addresses[lane]&~127U;r.lane_addresses=p[current].addresses;r.opcode=p[current].kind==STORE?1:0;for(int i=0;i<32;++i)if(((remaining>>i)&1U)&&(p[current].addresses[i]&~127U)==r.address)r.activeMask|=uint32_t(1)<<i;}out.cache=r;}return out;}
 void step(int request,bool reserve_ready,int idx,bool cache_ready,bool result_fire,bool end_fire){auto before=peek();int oldfifo=fifo;bool accept=request>=0&&before.ready;outstanding+=int(accept)-int(result_fire);require(outstanding>=0&&outstanding<=4,"LSU outstanding mismatch");if(before.dequeue)fifo=-1;if(accept)fifo=request;
 if(before.flush){phase=2;flushing=true;current=-1;remaining=0;flush_pending=false;}else if(before.dequeue){current=oldfifo;remaining=0xffffffff;phase=1;flushing=false;}else if(phase==1&&reserve_ready){entry=idx;phase=2;}else if(phase==2&&cache_ready){remaining&=~before.cache->activeMask;if(flushing||remaining==0)phase=0;flushing=false;}if(end_fire)flush_pending=true;}
};
struct RunResult{std::vector<ExecEvent>execution;std::vector<MemoryEvent>writes;int end=-1,host=-1,steps=0,skipped=0,last_cycle=-1;};
inline RunResult run(std::vector<Instruction>program,std::array<int,2>initial,int first_reset_free=2,int host_offer=3,bool skip=false){
 require(first_reset_free==2,"memory port fixed reset scope");addresses(program);Frontend core(program);LSU lsu(program);Coalescer co;ICacheMSHR ic(initial);Launch launch;MemorySystem memory;RunResult result;
 bool return_fifo=false;int return_phase=0;
 for(int t=first_reset_free;t<10000;++t){++result.steps;result.last_cycle=t;int req=core.request();auto lp=lsu.peek();auto mp=co.peek();auto iget=ic.peek();auto miss=core.fetch.miss_request();auto cache=memory.peek(t,iget);auto mo=memory.step(t,lp.cache,iget,mp.response_ready);ic.step(miss,mo.icache_ready,mo.icache_response);int allocated=co.step(lp.reserve,lp.mask,cache,core.result_ready(mp.tag));bool warp=!launch.finished&&launch.step(t,host_offer);
 auto eo=core.step(t,lp.ready,mp.valid,mp.tag,warp,mo.icache_fill);lsu.step(req,mp.reserve_ready,allocated,mo.request_ready,eo.result_fire,eo.end_fire);
 bool oldreturn=return_fifo;if(eo.end_fire){result.end=t;return_fifo=true;}if(return_phase==0&&oldreturn){return_fifo=false;return_phase=1;}else if(return_phase==1){result.host=t;return_phase=2;}
 int writes=0,maxwrite=0;if(memory.store)for(auto&e:memory.store->events)if(e.kind=="physical_write"){++writes;maxwrite=std::max(maxwrite,e.twice_cycle);}
 // Physical writes are scheduled on falling edges. Once the final write is
 // within this rising edge's following half-cycle, do not visit the next
 // rising edge: it lies beyond max(host return, last physical write).
 if(result.host>=0&&writes==16&&t*2+1>=maxwrite)break;
 if(skip&&core.ended&&core.arithmetic.empty()&&co.state==0&&!co.peek().valid&&std::none_of(co.used.begin(),co.used.end(),[&](int i){return co.mask[i]==0;})&&lsu.fifo<0&&!lsu.flush_pending&&lsu.phase!=1&&result.host>=0&&!ic.peek()&&ic.busy<0&&!core.fetch.miss_request()&&(!lsu.peek().cache||(memory.store&&memory.store->core))){int wake=memory.next_idle_wakeup(t);if(wake>t+1){result.skipped+=wake-t-1;t=wake-1;}}
 }
 result.execution=core.events;if(memory.store)for(auto&e:memory.store->events)if(e.kind=="physical_write")result.writes.push_back(e);require(result.host>=0&&result.writes.size()==16,"run did not complete bounded outputs");return result;
}
} // namespace ventus_timing
