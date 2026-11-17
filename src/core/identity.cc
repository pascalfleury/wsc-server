#include "src/core/identity.h"

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace wsc_server {
namespace {

constexpr std::string_view kIdFileName = "server-id";
constexpr std::string_view kTempSuffix = ".tmp";

// Quotes a path for error messages, so names with spaces stay readable.
std::string Quoted(const std::filesystem::path& p) {
  return absl::StrCat("\"", p.string(), "\"");
}

absl::StatusOr<Uuid> Load(const std::filesystem::path& file) {
  std::ifstream in(file);
  std::string line;
  if (!std::getline(in, line)) {
    return absl::DataLossError(
        absl::StrCat("cannot read server ID from ", Quoted(file)));
  }
  auto id = Uuid::Parse(absl::StripAsciiWhitespace(line));
  if (!id.ok()) {
    return absl::DataLossError(absl::StrCat(
        "invalid server ID in ", Quoted(file), ": ", id.status().message()));
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
    return absl::UnavailableError(
        absl::StrCat("cannot access ", Quoted(file), ": ", ec.message()));
  }

  std::filesystem::create_directories(state_dir, ec);
  if (ec) {
    return absl::UnavailableError(
        absl::StrCat("cannot create ", Quoted(state_dir), ": ", ec.message()));
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
      return absl::UnavailableError(absl::StrCat("cannot write ", Quoted(temp)));
    }
  }
  std::filesystem::rename(temp, file, ec);
  if (ec) {
    return absl::UnavailableError(
        absl::StrCat("cannot create ", Quoted(file), ": ", ec.message()));
  }
  return id;
}

}  // namespace wsc_server
