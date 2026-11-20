# Arbitration and rate shaping per output stream (docs/DATAPATH.org).
add_library(wsc_shaping
  event_queue.h policy.h stream_slot.h universe_buffer.cc universe_buffer.h)
wsc_target_defaults(wsc_shaping)
target_link_libraries(wsc_shaping PUBLIC wsc_codec absl::status absl::time
  PRIVATE absl::strings)

wsc_add_test(universe_buffer_test wsc_shaping)
wsc_add_test(stream_slot_test wsc_shaping)
wsc_add_test(event_queue_test wsc_shaping)
