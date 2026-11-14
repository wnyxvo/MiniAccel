#include "core/thread_pool.hpp"
#include "test.hpp"
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace miniaccel; using namespace miniaccel::internal;
int main() {
 for(int round=0;round<20;++round) {
  ThreadPool p(2,256); std::atomic<int> failures{0}, done{0};
  CHECK(p.submit({[]{throw std::runtime_error("failure");},[&](Status s){CHECK(s.code==StatusCode::InternalError);++failures;}}).ok());
  for(int i=0;i<100;++i) CHECK(p.submit({[&]{++done;},[&](Status){std::abort();}}).ok());
  std::thread t([&]{p.shutdown();}); p.shutdown(); t.join(); CHECK(done==100 && failures==1); CHECK(p.submit({[]{},[](Status){}}).code==StatusCode::Closed);
 }
 for(int round=0;round<50;++round) {
  ThreadPool p(4,32); std::atomic<int> accepted{0},executed{0}; std::atomic<bool> start{false}; std::vector<std::thread> producers;
  for(int i=0;i<4;++i) producers.emplace_back([&]{ while(!start.load()) std::this_thread::yield(); for(int j=0;j<200;++j) { auto s=p.submit({[&]{++executed;},[](Status){std::abort();}}); if(s.ok()) ++accepted; else CHECK(s.code==StatusCode::Closed || s.code==StatusCode::QueueFull); } });
  start=true; p.shutdown(); for(auto& t:producers)t.join(); CHECK(accepted==executed);
 }
}
