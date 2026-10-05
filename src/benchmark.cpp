// Copyright (c) 2026 memxlife
// Licensed under Mulan PSL v2; see LICENSE and NOTICE.
#include "execution_system.hpp"
#include <chrono>
int main(int argc,char**argv){try{
    using namespace ventus_timing;require(argc>=2,"program file required");
    const auto parse_start=std::chrono::steady_clock::now();
    std::ifstream input(argv[1]);require(bool(input),"cannot read input");int reset,offer,s0,s1;input>>reset>>offer>>s0>>s1;
    std::vector<Instruction> program;uint32_t pc,word;while(input>>std::hex>>pc>>word)program.push_back(decode(pc,word));require(!program.empty(),"empty program");
    if(argc>3&&std::string(argv[3])=="verify"){
        auto r=run(program,{s0,s1},reset,offer,true);
        for(const auto&e:r.execution)std::cout<<e.cycle<<'\t'<<e.kind<<'\t'<<e.pc<<'\t'<<e.order<<'\n';
        for(const auto&e:r.writes){std::cout<<e.twice_cycle<<"\tphysical_write\t"<<e.address;for(auto x:e.mask)std::cout<<'\t'<<x;std::cout<<'\n';}
        std::cout<<r.host<<"\thost_return\n";return 0;
    }
    const auto parse_end=std::chrono::steady_clock::now();
    int repeats=argc>2?std::stoi(argv[2]):1;require(repeats>0,"positive repetitions required");
    long long host=0,end=0,steps=0,skipped=0,endpoint=0,last_cycle=-1;size_t writes=0,execution=0;uint64_t checksum=0;
    const auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<repeats;++i){auto r=run(program,{s0,s1},reset,offer,true);host=r.host;end=r.end;endpoint=r.host*10+5;for(const auto&e:r.writes)endpoint=std::max(endpoint,static_cast<long long>(e.twice_cycle)*5+5);last_cycle=r.last_cycle;steps=r.steps;skipped=r.skipped;writes=r.writes.size();execution=r.execution.size();checksum+=uint64_t(r.host)+r.writes.size()+r.execution.size();}
    const auto finish=std::chrono::steady_clock::now();
    std::cout<<"{\"body_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(finish-begin).count()<<",\"parse_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end-parse_start).count()<<",\"repeats\":"<<repeats<<",\"host_cycle\":"<<host<<",\"end\":"<<end<<",\"endpoint_time\":"<<endpoint<<",\"last_rising_time\":"<<last_cycle*10+5<<",\"steps\":"<<steps<<",\"skipped\":"<<skipped<<",\"writes\":"<<writes<<",\"execution_events\":"<<execution<<",\"checksum\":"<<checksum<<",\"payload_evaluations\":0}\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
