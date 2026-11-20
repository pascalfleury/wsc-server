# Frames and the frame pool (docs/DATAPATH.org, D-26).
add_library(wsc_memory frame.h frame_pool.cc frame_pool.h)
wsc_target_defaults(wsc_memory)
target_link_libraries(wsc_memory PUBLIC
  wsc_core absl::status absl::statusor absl::synchronization absl::time)

wsc_add_test(frame_pool_test wsc_memory wsc_testing)
