#ifndef WSC_SERVER_SRC_CORE_IDENTITY_H_
#define WSC_SERVER_SRC_CORE_IDENTITY_H_

#include <filesystem>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/statusor.h"
#include "src/core/uuid.h"

namespace wsc_server {

// Returns the server ID stored in `state_dir`. On first start it creates the
// directory, generates an ID from `gen` and persists it (F-01).
//
// An unreadable or malformed ID file is an error: the ID is never silently
// replaced, because consoles and configuration refer to it.
absl::StatusOr<Uuid> LoadOrCreateServerId(
    const std::filesystem::path& state_dir, absl::BitGenRef gen);

}  // namespace wsc_server

#endif  // WSC_SERVER_SRC_CORE_IDENTITY_H_
