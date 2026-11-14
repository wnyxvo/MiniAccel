#include "core/bounded_queue.hpp"
#include "test.hpp"
#include <atomic>
#include <thread>
#include <vector>
using namespace miniaccel; using namespace miniaccel::internal;
int main() {
 for(int round=0;round<10;++round) { BoundedQueue<int> q(8); std::vector<std::atomic<int>> seen(400); for(auto& x:seen)x=0; std::vector<std::thread> consumers,producers;
 for(int i=0;i<4;++i) consumers.emplace_back([&]{ for(;;) { auto x=q.pop(); if(!x.ok()) break; ++seen[x.value()]; } });
 for(int p=0;p<4;++p) producers.emplace_back([&,p]{ for(int j=0;j<100;++j) { while(true) { auto s=q.push(p*100+j); if(s.ok()) break; CHECK(s.code==StatusCode::QueueFull); std::this_thread::yield(); } CHECK(q.size()<=8); } });
 for(auto& t:producers) { t.join(); } q.close(); for(auto& t:consumers)t.join(); for(auto& x:seen) CHECK(x==1); CHECK(q.push(1).code==StatusCode::Closed); CHECK(q.pop().status().code==StatusCode::Closed); }
 BoundedQueue<int> q(1); CHECK(q.push(1).ok()); CHECK(q.push(2).code==StatusCode::QueueFull); q.close(); CHECK(q.pop().value()==1); CHECK(!q.pop().ok());
}
