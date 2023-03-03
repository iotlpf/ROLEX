#pragma once

#if defined(__GNUC__) && __GNUC__ < 7
#include <experimental/optional>
#define _r2_optional std::experimental::optional
#else
#include <optional>
#define _r2_optional std::optional
#endif

namespace r2 {
template <typename T> using Option = _r2_optional<T>;
} // namespace r2
