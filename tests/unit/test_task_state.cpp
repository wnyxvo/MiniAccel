#include "core/task_state.hpp"
#include "test.hpp"
#include <thread>
#include <vector>
#include <atomic>
using namespace miniaccel; using namespace miniaccel::internal;
int main() {
 TokenAllocator a(1,1,UINT64_MAX-1), b(2,1), c(1,2); CHECK(a.allocate().value().sequence == UINT64_MAX-1); CHECK(a.allocate().value().sequence == UINT64_MAX); CHECK(!a.allocate().ok()); CHECK(b.allocate().value() != TaskToken({1,1,1})); CHECK(c.allocate().value() != TaskToken({1,1,1}));
 TokenAllocator shared(8,1); std::vector<TaskToken> tokens(1000); std::vector<std::thread> allocators; for(int i=0;i<4;++i) allocators.emplace_back([&,i]{for(int j=0;j<250;++j) tokens[i*250+j]=shared.allocate().value();}); for(auto& t:allocators)t.join(); std::vector<bool> seen(1001); for(auto token:tokens) { CHECK(token.sequence>0 && token.sequence<=1000 && !seen[token.sequence]); seen[token.sequence]=true; }
 TaskRecord t({1,1,1}); CHECK(!t.transition(TaskState::Completed).ok()); CHECK(t.transition(TaskState::WaitingDependencies).ok()); CHECK(!t.transition(TaskState::Submitted).ok()); CHECK(t.transition(TaskState::Ready).ok()); CHECK(!t.transition(TaskState::Completed).ok()); CHECK(t.transition(TaskState::Submitted).ok()); CHECK(!t.transition(TaskState::Canceled,{StatusCode::Canceled,"canceled"}).ok()); CHECK(t.wait(0).status().code == StatusCode::Timeout); CHECK(t.query().state == TaskState::Submitted);
 std::atomic<int> won{0}; std::vector<std::thread> v; for (int i=0;i<16;++i) v.emplace_back([&]{ if(t.transition(TaskState::Completed).ok()) ++won; }); for(auto& x:v)x.join(); CHECK(won==1); CHECK(t.wait(0).value().state==TaskState::Completed); CHECK(!t.transition(TaskState::Failed,{StatusCode::InternalError,"late"}).ok());
 TaskRecord failure({1,1,2}); CHECK(failure.transition(TaskState::WaitingDependencies).ok()); CHECK(failure.transition(TaskState::Failed,{StatusCode::DependencyFailed,"dependency"}).ok()); CHECK(!failure.transition(TaskState::Canceled,{StatusCode::Canceled,"late"}).ok()); CHECK(failure.query().status.code==StatusCode::DependencyFailed);
 TaskRecord canceled({1,1,3}); CHECK(canceled.transition(TaskState::Canceled,{StatusCode::Canceled,"closed"}).ok()); CHECK(!canceled.transition(TaskState::WaitingDependencies).ok());
}
