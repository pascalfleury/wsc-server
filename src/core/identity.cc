#include "src/core/identity.h"

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/strings/ascii.h"
#include "src/core/status_builder.h"

namespace wsc_server {
namespace {

constexpr std::string_view kIdFileName = "server-id";
constexpr std::string_view kTempSuffix = ".tmp";

absl::StatusOr<Uuid> Load(const std::filesystem::path& file) {
  std::ifstream in(file);
  std::string line;
  if (!std::getline(in, line)) {
    return DataLossErrorBuilder() << "cannot read server ID from " << file;
  }
  auto id = Uuid::Parse(absl::StripAsciiWhitespace(line));
  if (!id.ok()) {
    return DataLossErrorBuilder() << "invalid server ID in " << file << ": "
                                  << id.status().message();
  }
  return id;
}

}  // namespace

absl::StatusOr<Uuid> LoadOrCreateServerId(
    const std::filesystem::path& state_dir, absl::BitGenRef gen) {
  const std::filesystem::path file = state_dir / kIdFileName;
  std::error_code ec;
  if (std::filesystem::exists(file, ec)) return Load(file);
  if (ec) {
    return UnavailableErrorBuilder()
           << "cannot access " << file << ": " << ec.message();
  }

  std::filesystem::create_directories(state_dir, ec);
  if (ec) {
    return UnavailableErrorBuilder()
           << "cannot create " << state_dir << ": " << ec.message();
  }

  // Write to a temporary file and rename, so a crash never leaves a partial ID.
  const Uuid id = Uuid::GenerateV4(gen);
  std::filesystem::path temp = file;
  temp += kTempSuffix;  // appends to the file name, no separator
  {
    std::ofstream out(temp, std::ios::trunc);
    out << id.ToString() << '\n';
    out.flush();
    if (!out) {
      return UnavailableErrorBuilder() << "cannot write " << temp;
    }
  }
  std::filesystem::rename(temp, file, ec);
  if (ec) {
    return UnavailableErrorBuilder()
           << "cannot create " << file << ": " << ec.message();
  }
  return id;
}

}  // namespace wsc_server
