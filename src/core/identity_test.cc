#include "src/core/identity.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

class IdentityTest : public testing::Test {
 protected:
  void SetUp() override {
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    state_dir_ = std::filesystem::path(testing::TempDir()) /
                 (std::string("wsc_identity_") + info->name());
    std::filesystem::remove_all(state_dir_, ignored_);
  }
  void TearDown() override {
    std::filesystem::remove_all(state_dir_, ignored_);
  }

  std::filesystem::path state_dir_;
  std::error_code ignored_;
  std::mt19937 gen_{1};
};

TEST_F(IdentityTest, FirstStartCreatesDirectoryAndPersistsId) {
  const auto id = LoadOrCreateServerId(state_dir_ / "nested", gen_);
  ASSERT_TRUE(id.ok()) << id.status();

  std::ifstream in(state_dir_ / "nested" / "server-id");
  std::string line;
  ASSERT_TRUE(std::getline(in, line));
  EXPECT_EQ(line, id->ToString());
  EXPECT_FALSE(
      std::filesystem::exists(state_dir_ / "nested" / "server-id.tmp"));
}

TEST_F(IdentityTest, LaterStartsReturnTheSameId) {
  const auto first = LoadOrCreateServerId(state_dir_, gen_);
  ASSERT_TRUE(first.ok()) << first.status();
  const auto second = LoadOrCreateServerId(state_dir_, gen_);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(*second, *first);
}

TEST_F(IdentityTest, MalformedIdFileIsAnErrorAndIsNotOverwritten) {
  std::filesystem::create_directories(state_dir_);
  { std::ofstream(state_dir_ / "server-id") << "not-a-uuid\n"; }

  const auto id = LoadOrCreateServerId(state_dir_, gen_);
  EXPECT_EQ(id.status().code(), absl::StatusCode::kDataLoss);

  std::ifstream in(state_dir_ / "server-id");
  std::string line;
  ASSERT_TRUE(std::getline(in, line));
  EXPECT_EQ(line, "not-a-uuid");
}

TEST_F(IdentityTest, EmptyIdFileIsAnError) {
  std::filesystem::create_directories(state_dir_);
  { std::ofstream touch(state_dir_ / "server-id"); }
  EXPECT_EQ(LoadOrCreateServerId(state_dir_, gen_).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST_F(IdentityTest, StateDirThatIsAFileIsAnError) {
  { std::ofstream touch(state_dir_); }
  EXPECT_EQ(LoadOrCreateServerId(state_dir_, gen_).status().code(),
            absl::StatusCode::kUnavailable);
}

}  // namespace
}  // namespace wsc_server
