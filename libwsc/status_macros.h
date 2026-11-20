#ifndef WSC_SERVER_LIBWSC_STATUS_MACROS_H_
#define WSC_SERVER_LIBWSC_STATUS_MACROS_H_

#include "absl/status/status.h"

// Evaluates `expr` (an absl::Status) once and returns it from the enclosing
// function if it is not OK. Works in functions returning absl::Status or
// absl::StatusOr<T>.
#define RETURN_IF_ERROR(expr)                           \
  do {                                                  \
    if (::absl::Status return_if_error_status = (expr); \
        !return_if_error_status.ok()) {                 \
      return return_if_error_status;                    \
    }                                                   \
  } while (false)

#endif  // WSC_SERVER_LIBWSC_STATUS_MACROS_H_
