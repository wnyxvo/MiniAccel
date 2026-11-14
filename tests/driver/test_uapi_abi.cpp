// Verify this header can also be parsed from the C++17 Runtime.
#include "miniaccel_uapi.h"
#include <type_traits>
static_assert(std::is_standard_layout<ma_device_query>::value, "C++ layout");
static_assert(std::is_standard_layout<ma_completion_record>::value, "C++ layout");
int main() { return MA_UAPI_VERSION != 1u; }
