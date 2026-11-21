#ifndef WSC_SERVER_SRC_PLATFORM_ASIO_H_
#define WSC_SERVER_SRC_PLATFORM_ASIO_H_

// The only way project code includes Asio. Link the `wsc_asio` CMake target,
// which sets ASIO_NO_EXCEPTIONS; Asio then expects the application to define
// asio::detail::throw_exception.

#include "absl/log/log.h"
#include "asio/detail/config.hpp"
#include "asio/detail/throw_exception.hpp"

// The inline-namespace macro sits between the two, so they cannot be merged.
namespace asio {  // NOLINT(modernize-concat-nested-namespaces)
ASIO_INLINE_NAMESPACE_BEGIN
namespace detail {

// Exceptions are disabled (N-04). Asio only gets here from calls that have no
// std::error_code overload, where a failure is a programming or system error.
template <typename Exception>
void throw_exception(const Exception& e ASIO_SOURCE_LOCATION_PARAM) {
  LOG(FATAL) << "fatal Asio error: " << e.what();
}

}  // namespace detail
ASIO_INLINE_NAMESPACE_END
}  // namespace asio

#include "asio.hpp"  // NOLINT(build/include_order): needs the definition above

#endif  // WSC_SERVER_SRC_PLATFORM_ASIO_H_
