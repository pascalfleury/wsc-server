#ifndef WSC_SERVER_SRC_PLATFORM_ASIO_H_
#define WSC_SERVER_SRC_PLATFORM_ASIO_H_

// The only way project code includes Asio. Link the `wsc_asio` CMake target,
// which sets ASIO_NO_EXCEPTIONS; Asio then expects the application to define
// asio::detail::throw_exception.

#include <cstdio>
#include <cstdlib>

#include "asio/detail/config.hpp"
#include "asio/detail/throw_exception.hpp"

namespace asio {
ASIO_INLINE_NAMESPACE_BEGIN
namespace detail {

// Exceptions are disabled (N-04). Asio only gets here from calls that have no
// std::error_code overload, where a failure is a programming or system error.
template <typename Exception>
void throw_exception(const Exception& e ASIO_SOURCE_LOCATION_PARAM) {
  std::fprintf(stderr, "fatal Asio error: %s\n", e.what());
  std::abort();
}

}  // namespace detail
ASIO_INLINE_NAMESPACE_END
}  // namespace asio

#include "asio.hpp"  // NOLINT(build/include_order): needs the definition above

#endif  // WSC_SERVER_SRC_PLATFORM_ASIO_H_
