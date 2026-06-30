cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(local_device_portal)

target_sources(app PRIVATE
  src/main.c
  src/credential_store.c
  src/portal_dns.c
  src/portal_http.c
  src/portal_mdns.c
  src/portal_render.c
  src/portal_state.c
  src/wifi_manager.c
)
