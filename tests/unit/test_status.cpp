#include "miniaccel/status.hpp"
#include "test.hpp"
using namespace miniaccel;
static Result<int> propagate() { Result<int> r(Status{StatusCode::IoError, "io"}); if (!r.ok()) return r.status(); return r.value(); }
int main() { auto r = propagate(); CHECK(!r.ok()); CHECK(r.status().code == StatusCode::IoError); bool threw = false; try { (void)r.value(); } catch (const std::bad_optional_access&) { threw = true; } CHECK(threw); Result<int> ok(42); CHECK(ok.ok() && ok.value() == 42 && ok.status().ok()); }
