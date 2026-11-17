#include "src/platform/paths.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

// An EnvLookup over a fixed set of variables.
class FakeEnv {
 public:
  explicit FakeEnv(std::map<std::string, std::string, std::less<>> vars)
      : vars_(std::move(vars)) {}

  std::optional<std::string> operator()(std::string_view name) const {
    const auto it = vars_.find(name);
    if (it == vars_.end()) return std::nullopt;
    return it->second;
  }

 private:
  std::map<std::string, std::string, std::less<>> vars_;
};

using Path = std::filesystem::path;

TEST(DefaultStateDirTest, FailsWhenNoVariableIsSet) {
  const FakeEnv env({});
  EXPECT_EQ(DefaultStateDir(env).status().code(), absl::StatusCode::kNotFound);
}

#if defined(_WIN32)

TEST(DefaultStateDirTest, UsesAppData) {
  const FakeEnv env({{"APPDATA", "C:\\Users\\me\\AppData\\Roaming"}});
  const auto dir = DefaultStateDir(env);
  ASSERT_TRUE(dir.ok()) << dir.status();
  EXPECT_EQ(*dir, Path("C:\\Users\\me\\AppData\\Roaming") / "wsc-server");
}

#elif defined(__APPLE__)

TEST(DefaultStateDirTest, UsesApplicationSupportUnderHome) {
  const FakeEnv env({{"HOME", "/Users/me"}});
  const auto dir = DefaultStateDir(env);
  ASSERT_TRUE(dir.ok()) << dir.status();
  EXPECT_EQ(*dir, Path("/Users/me/Library/Application Support/wsc-server"));
}

#else

TEST(DefaultStateDirTest, PrefersXdgConfigHome) {
  const FakeEnv env({{"XDG_CONFIG_HOME", "/xdg"}, {"HOME", "/home/me"}});
  const auto dir = DefaultStateDir(env);
  ASSERT_TRUE(dir.ok()) << dir.status();
  EXPECT_EQ(*dir, Path("/xdg/wsc-server"));
}

TEST(DefaultStateDirTest, FallsBackToDotConfigUnderHome) {
  const FakeEnv env({{"HOME", "/home/me"}});
  const auto dir = DefaultStateDir(env);
  ASSERT_TRUE(dir.ok()) << dir.status();
  EXPECT_EQ(*dir, Path("/home/me/.config/wsc-server"));
}

TEST(DefaultStateDirTest, IgnoresRelativeXdgConfigHome) {
  const FakeEnv env({{"XDG_CONFIG_HOME", "relative"}, {"HOME", "/home/me"}});
  const auto dir = DefaultStateDir(env);
  ASSERT_TRUE(dir.ok()) << dir.status();
  EXPECT_EQ(*dir, Path("/home/me/.config/wsc-server"));
}

#endif

TEST(ProcessEnvTest, ReturnsNulloptForUnsetVariable) {
  EXPECT_EQ(ProcessEnv("WSC_SERVER_TEST_SURELY_UNSET_VARIABLE"), std::nullopt);
}

TEST(ProcessEnvTest, ReturnsValueOfSetVariable) {
  // PATH is set on every platform the tests run on.
  EXPECT_TRUE(ProcessEnv("PATH").has_value());
}

}  // namespace
}  // namespace wsc_server
