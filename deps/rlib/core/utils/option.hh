#pragma once

#if defined(__GNUC__) && __GNUC__ < 7
#include <experimental/optional>
#define _rlib_optional std::experimental::optional
#else
#include <optional>
#define _rlib_optional std::optional
#endif

namespace rdmaio {
template <typename T> using Option = _rlib_optional<T>;
}
