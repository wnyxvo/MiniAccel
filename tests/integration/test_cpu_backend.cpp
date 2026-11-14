#include "backends/cpu/cpu_backend.hpp"
#include "test.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>
using namespace miniaccel; using namespace miniaccel::internal;
struct Fixture {
 CpuBackend backend{2,64,128}; BackendContextHandle ctx;
 Fixture() { CHECK(backend.initialize().ok()); ctx=backend.createContext({256,64}).value(); }
 BackendBuffer buffer(size_t size=64) { return backend.allocateBuffer(ctx,size,64).value(); }
 BackendCompletion run(BackendCommand c) { BackendTaskId id=0; CHECK(backend.submit(ctx,c,&id).ok()); CHECK(id); std::vector<BackendCompletion> out; CHECK(backend.pollCompletions(out,3000).ok()); CHECK(out.size()==1 && out[0].id==id); CHECK(out[0].state==TaskState::Completed && out[0].status.ok()); return out[0]; }
};
static void ut006() {
 Fixture f; CHECK(f.backend.capabilities().opcode_bits==31); BackendCommand c; f.run(c);
 const Opcode ops[]={Opcode::Nop,Opcode::Delay,Opcode::Fill,Opcode::Copy,Opcode::VectorAdd};
 for(size_t i=0;i<5;++i) {
  CHECK(f.backend.capabilities().supports(ops[i]));
  CHECK(opcodeCapabilityBit(ops[i])==(uint64_t{1}<<i));
  BackendCapabilities only{opcodeCapabilityBit(ops[i])};
  for(size_t j=0;j<5;++j) CHECK(only.supports(ops[j])==(i==j));
 }
 for(auto raw:{0u,6u,64u,65u,UINT32_MAX}) {
  auto invalid=static_cast<Opcode>(raw);
  CHECK(opcodeCapabilityBit(invalid)==0); CHECK(!f.backend.capabilities().supports(invalid));
  CHECK(!(BackendCapabilities{UINT64_MAX}.supports(invalid)));
 }
 c.opcode=Opcode::Delay; c.delay_ms=0; f.run(c); c.delay_ms=1000;
 BackendTaskId id=0; auto start=std::chrono::steady_clock::now(); CHECK(f.backend.submit(f.ctx,c,&id).ok()); std::vector<BackendCompletion> out; CHECK(f.backend.pollCompletions(out,0).code==StatusCode::Timeout); CHECK(f.backend.pollCompletions(out,3000).ok()); CHECK(out.size()==1 && out[0].id==id); CHECK(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(900));
 c.delay_ms=1001; id=77; CHECK(f.backend.submit(f.ctx,c,&id).code==StatusCode::InvalidArgument); CHECK(id==77);
 // Completion records survive shutdown and a blocked poll is awakened.
 CpuBackend b(1,2,2); CHECK(b.initialize().ok()); auto ctx=b.createContext({}).value(); c={}; CHECK(b.submit(ctx,c,&id).ok()); b.shutdown(); out.clear(); CHECK(b.pollCompletions(out,0).ok()); CHECK(out.size()==1 && out[0].id==id); CHECK(b.pollCompletions(out,0).code==StatusCode::Closed);
 CpuBackend idle; CHECK(idle.initialize().ok()); std::atomic<bool> closed{false}; std::thread waiter([&]{std::vector<BackendCompletion> v; closed=(idle.pollCompletions(v,-1).code==StatusCode::Closed);}); idle.shutdown(); waiter.join(); CHECK(closed);
}
static void ut007() {
 Fixture f; auto b=f.buffer(); auto p=static_cast<unsigned char*>(f.backend.mapBuffer(b.handle).value()); std::memset(p,0,64);
 BackendCommand c; c.opcode=Opcode::Fill; c.buffers={b.handle}; c.dst_offset=3;c.bytes=61;c.fill_byte=0xab; f.run(c); for(size_t i=0;i<64;++i) CHECK(p[i]==(i<3?0:0xab));
 c.dst_offset=64;c.bytes=0; f.run(c); c.dst_offset=65; BackendTaskId id=9; CHECK(f.backend.submit(f.ctx,c,&id).code==StatusCode::InvalidArgument); CHECK(id==9); c.dst_offset=UINT64_MAX;c.bytes=8; CHECK(f.backend.submit(f.ctx,c,&id).code==StatusCode::InvalidArgument); CHECK(f.backend.unmapBuffer(b.handle).ok()); CHECK(f.backend.releaseBuffer(b.handle).ok());
}
static void ut008() {
 Fixture f; auto b=f.buffer(); auto p=static_cast<unsigned char*>(f.backend.mapBuffer(b.handle).value()); for(unsigned i=0;i<64;++i)p[i]=static_cast<unsigned char>(i); unsigned char expected[64]; std::memcpy(expected,p,64); std::memmove(expected+7,expected,40);
 BackendCommand c; c.opcode=Opcode::Copy;c.buffers={b.handle,b.handle}; c.dst_offset=7;c.bytes=40; f.run(c); CHECK(std::memcmp(p,expected,64)==0);
 auto d=f.buffer(); auto q=static_cast<unsigned char*>(f.backend.mapBuffer(d.handle).value()); c.buffers={b.handle,d.handle};c.src_offset=10;c.dst_offset=2;c.bytes=40; f.run(c); CHECK(std::memcmp(q+2,p+10,40)==0);
 c.src_offset=64;c.dst_offset=64;c.bytes=0; f.run(c); c.src_offset=65; BackendTaskId id=0; CHECK(f.backend.submit(f.ctx,c,&id).code==StatusCode::InvalidArgument); CHECK(id==0);
 CHECK(f.backend.unmapBuffer(b.handle).ok()); CHECK(f.backend.unmapBuffer(d.handle).ok());
}
static void ut009() {
 Fixture f; auto a=f.buffer(),b=f.buffer(),c=f.buffer(); auto pa=static_cast<float*>(f.backend.mapBuffer(a.handle).value()); auto pb=static_cast<float*>(f.backend.mapBuffer(b.handle).value()); auto pc=static_cast<float*>(f.backend.mapBuffer(c.handle).value());
 for(int i=0;i<16;++i) { pa[i]=i*0.25f; pb[i]=i*-0.37f; }
 BackendCommand cmd;cmd.opcode=Opcode::VectorAdd;cmd.buffers={a.handle,b.handle,c.handle};cmd.bytes=64; f.run(cmd); for(int i=0;i<16;++i) { float ref=pa[i]+pb[i]; CHECK(std::fabs(pc[i]-ref)<=1e-6f+1e-5f*std::fabs(ref)); }
 cmd.src_offset=64;cmd.dst_offset=64;cmd.bytes=0; f.run(cmd); BackendTaskId id=0; cmd.src_offset=1; CHECK(f.backend.submit(f.ctx,cmd,&id).code==StatusCode::InvalidArgument); cmd.src_offset=0;cmd.dst_offset=0;cmd.bytes=16;cmd.buffers[2]=a.handle; CHECK(f.backend.submit(f.ctx,cmd,&id).code==StatusCode::InvalidArgument); CHECK(id==0);
 CHECK(f.backend.unmapBuffer(a.handle).ok()); CHECK(f.backend.unmapBuffer(b.handle).ok()); CHECK(f.backend.unmapBuffer(c.handle).ok());
}
static void ut010() {
 Fixture f; BackendTaskId id=123; BackendCommand cmd; cmd.opcode=static_cast<Opcode>(999); CHECK(f.backend.submit(f.ctx,cmd,&id).code==StatusCode::Unsupported); CHECK(id==123);
 cmd.opcode=Opcode::Fill; cmd.buffers={999}; cmd.bytes=0; CHECK(f.backend.submit(f.ctx,cmd,&id).code==StatusCode::InvalidHandle); auto other=f.backend.createContext({}).value();auto b=f.backend.allocateBuffer(other,64,64).value();cmd.buffers={b.handle}; CHECK(f.backend.submit(f.ctx,cmd,&id).code==StatusCode::WrongContext); CHECK(f.run({}).id==1); CHECK(f.backend.releaseBuffer(b.handle).ok()); CHECK(f.backend.destroyContext(other).ok());
#ifndef NDEBUG
 CpuBackend faulty(1,4,4); CHECK(faulty.initialize().ok()); auto fault_ctx=faulty.createContext({}).value(); faulty.failNextExecutionForTest(); BackendCommand fault_cmd; CHECK(faulty.submit(fault_ctx,fault_cmd,&id).ok()); std::vector<BackendCompletion> failed; CHECK(faulty.pollCompletions(failed,3000).ok()); CHECK(failed.size()==1 && failed[0].id==id && failed[0].state==TaskState::Failed && failed[0].status.code==StatusCode::InternalError); CHECK(!failed[0].status.message.empty()); failed.clear(); CHECK(faulty.pollCompletions(failed,0).code==StatusCode::Timeout); CHECK(faulty.submit(fault_ctx,fault_cmd,&id).ok()); CHECK(faulty.pollCompletions(failed,3000).ok()); CHECK(failed.size()==1 && failed[0].state==TaskState::Completed);
#endif
 CpuBackend limited(1,4,1); CHECK(limited.initialize().ok()); auto ctx=limited.createContext({256,4}).value(); BackendCommand nop; CHECK(limited.submit(ctx,nop,&id).ok()); id=88; CHECK(limited.submit(ctx,nop,&id).code==StatusCode::QueueFull); CHECK(id==88); std::vector<BackendCompletion> v; CHECK(limited.pollCompletions(v,3000).ok()); CHECK(limited.submit(ctx,nop,&id).ok()); CHECK(id==2); limited.shutdown();
 // Fast completions are retained until consumed, with one unique ID per task.
 CpuBackend fast(4,256,256); CHECK(fast.initialize().ok()); ctx=fast.createContext({256,256}).value(); std::set<uint64_t> accepted, completed;
 while(accepted.size()<10000) { if(fast.submit(ctx,nop,&id).ok()) accepted.insert(id); else { v.clear(); CHECK(fast.pollCompletions(v,3000).ok()); for(auto& record:v) CHECK(completed.insert(record.id).second); } }
 fast.shutdown(); while(true) { v.clear(); auto s=fast.pollCompletions(v,0); if(s.code==StatusCode::Closed) break; CHECK(s.ok()); for(auto& record:v) CHECK(completed.insert(record.id).second); } CHECK(accepted==completed);
 // Release after admission does not invalidate the worker's strong reference.
 for(int i=0;i<20;++i) { CpuBackend life(1,4,4); CHECK(life.initialize().ok()); auto c=life.createContext({}).value(); auto data=life.allocateBuffer(c,64,64).value(); BackendCommand delay;delay.opcode=Opcode::Delay;delay.delay_ms=1; CHECK(life.submit(c,delay,&id).ok()); BackendCommand fill;fill.opcode=Opcode::Fill;fill.buffers={data.handle};fill.bytes=64;fill.fill_byte=17; CHECK(life.submit(c,fill,&id).ok()); CHECK(life.releaseBuffer(data.handle).ok()); life.shutdown(); v.clear(); CHECK(life.pollCompletions(v,0).ok()); CHECK(v.size()==2); CHECK(life.destroyContext(c).ok()); }
}
int main(int argc,char** argv) { CHECK(argc==2); auto name=std::string(argv[1]); if(name=="006")ut006(); else if(name=="007")ut007(); else if(name=="008")ut008(); else if(name=="009")ut009(); else if(name=="010")ut010(); else std::abort(); }
