#include "src/core/app.h"

#include <algorithm>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "libwsc/status_macros.h"
#include "src/core/clock.h"
#include "src/core/identity.h"
#include "src/memory/frame_pool.h"
#include "src/outputs/sacn/sacn_adapter.h"
#include "src/outputs/sacn/sacn_streams.h"
#include "src/outputs/udp_sender.h"
#include "src/platform/asio.h"
#include "src/routing/router.h"
#include "src/sessions/ws_server.h"

namespace wsc_server {
namespace {

constexpr absl::Duration kSweepInterval = absl::Seconds(1);

// Frees idle frames once a second (DATAPATH.org, idle eviction).
void SweepPeriodically(asio::steady_timer& timer, FramePool& pool) {
  timer.expires_after(absl::ToChronoNanoseconds(kSweepInterval));
  timer.async_wait([&timer, &pool](const asio::error_code& ec) {
    if (ec) return;
    pool.Sweep();
    SweepPeriodically(timer, pool);
  });
}

unsigned ThreadCount() {
  return std::clamp(std::thread::hardware_concurrency(), 2u, 4u);
}

}  // namespace

absl::StatusOr<std::unique_ptr<App>> App::Create(const Options& options) {
  absl::BitGen gen;
  const auto server_id = LoadOrCreateServerId(options.state_dir, gen);
  if (!server_id.ok()) return server_id.status();
  return absl::WrapUnique(new App(*server_id, options));
}

absl::Status App::Run(std::stop_token stop) {
  // Declaration order is teardown order in reverse: the threads are joined
  // before the components their posted work refers to are destroyed.
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  const SystemClock clock;
  FramePool pool(clock);
  Router router;

  absl::StatusOr<std::unique_ptr<UdpSender>> udp = UdpSender::Create(io);
  if (!udp.ok()) return udp.status();
  SacnAdapter sacn(io.get_executor(), clock,
                   {.cid = SacnCid(server_id_, "default")}, **udp);
  RETURN_IF_ERROR(router.Register(sacn));

  asio::steady_timer sweep(io);
  SweepPeriodically(sweep, pool);

  std::vector<std::jthread> threads;
  threads.reserve(ThreadCount());
  for (unsigned i = 0; i < ThreadCount(); ++i) {
    threads.emplace_back([&io] { io.run(); });
  }
  // Stops the threads on every return path below.
  const auto stop_threads = [&] {
    work.reset();
    io.stop();
    threads.clear();  // joins
  };

  absl::StatusOr<std::unique_ptr<WsServer>> ws = WsServer::Create(
      {.port = options_.port, .bind_address = options_.bind_address},
      io.get_executor(), router, pool, clock);
  if (!ws.ok()) {
    stop_threads();
    return ws.status();
  }

  LOG(INFO) << "wsc-server " << server_id_.ToString()
            << " running, WebSocket on port " << (*ws)->port() << " /ws";
  {
    absl::Mutex mutex;
    bool stopped = false;
    const std::stop_callback on_stop(stop, [&] {
      absl::MutexLock lock(mutex);
      stopped = true;
    });
    absl::MutexLock lock(mutex);
    mutex.Await(absl::Condition(&stopped));
  }
  ws->reset();  // closes the connections first
  stop_threads();
  WsServer::ShutdownLibrary();
  LOG(INFO) << "wsc-server stopped";
  return absl::OkStatus();
}

}  // namespace wsc_server
