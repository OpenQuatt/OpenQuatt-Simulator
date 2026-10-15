#include "esphome/core/defines.h"

#ifdef USE_WEBSERVER
#include "esp_http_server.h"

// The native 4352-byte HTTPD stack had only 128 bytes left in HIL when
// serializing entity details alongside SSE. Preserve the native server's
// lifecycle, allocation capabilities and failure handling; reserve another
// 3840 bytes of internal RAM rather than moving a networking stack to PSRAM.
extern "C" esp_err_t __real_httpd_start(httpd_handle_t *handle,
                                        const httpd_config_t *config);

extern "C" esp_err_t __wrap_httpd_start(httpd_handle_t *handle,
                                        const httpd_config_t *config) {
  if (handle == nullptr || config == nullptr)
    return __real_httpd_start(handle, config);

  httpd_config_t adjusted = *config;
  if (adjusted.stack_size < 8192U)
    adjusted.stack_size = 8192U;
  // ESP-IDF copies this configuration before starting the task. Never lower
  // an upstream larger stack, and propagate allocation failures unchanged.
  return __real_httpd_start(handle, &adjusted);
}
#endif
