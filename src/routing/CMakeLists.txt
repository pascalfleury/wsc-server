# Stateless forward routing and the OutputAdapter interface (docs/DATAPATH.org).
add_library(wsc_routing message.h output_adapter.h router.cc router.h)
wsc_target_defaults(wsc_routing)
target_link_libraries(wsc_routing PUBLIC wsc_codec wsc_memory wsc_shaping
  absl::flat_hash_map absl::status PRIVATE absl::strings)

wsc_add_test(router_test wsc_routing)
