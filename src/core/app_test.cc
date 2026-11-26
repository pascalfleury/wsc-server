#include "src/core/app.h"

#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace wsc_server {
namespace {

class AppTest : public testing::Test {
 protected:
  void SetUp() override {
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    options_.state_dir = std::filesystem::path(testing::TempDir()) /
                         (std::string("wsc_app_") + info->name());
    std::filesystem::remove_all(options_.state_dir, ignored_);
    options_.port = 0;  // never clash with a running server
    options_.bind_address = "127.0.0.1";
  }
  void TearDown() override {
    std::filesystem::remove_all(options_.state_dir, ignored_);
  }

  App::Options options_;
  std::error_code ignored_;
};

TEST_F(AppTest, KeepsItsServerIdAcrossRestarts) {
  const auto first = App::Create(options_);
  ASSERT_TRUE(first.ok()) << first.status();
  const auto second = App::Create(options_);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ((*second)->server_id(), (*first)->server_id());
}

TEST_F(AppTest, CreateFailsWhenStateDirCannotBeUsed) {
  { std::ofstream touch(options_.state_dir); }  // a file, not a directory
  EXPECT_FALSE(App::Create(options_).ok());
}

TEST_F(AppTest, RunReturnsAtOnceWhenStopWasAlreadyRequested) {
  const auto app = App::Create(options_);
  ASSERT_TRUE(app.ok()) << app.status();
  std::stop_source stop;
  stop.request_stop();
  EXPECT_TRUE((*app)->Run(stop.get_token()).ok());
}

TEST_F(AppTest, RunReturnsWhenStopIsRequestedFromAnotherThread) {
  const auto app = App::Create(options_);
  ASSERT_TRUE(app.ok()) << app.status();
  std::stop_source stop;
  std::jthread stopper([&stop] { stop.request_stop(); });
  EXPECT_TRUE((*app)->Run(stop.get_token()).ok());
}

}  // namespace
}  // namespace wsc_server
