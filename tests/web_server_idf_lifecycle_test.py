#!/usr/bin/env python3
"""Compile actual SSE send/close methods against deterministic HTTPD/clock stubs.

This tests production method bodies, not a separately implemented policy. It
does not emulate the real HTTPD work queue, scheduler, sockets or allocator.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    """Extract a whole named production method; fail if its shape disappears."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    component = ROOT / "components/web_server_idf"
    cpp = (component / "web_server_idf.cpp").read_text()
    header = (component / "web_server_idf.h").read_text()
    bodies = "\n".join(method(cpp, "void AsyncEventSourceResponse::" + name)
                       for name in ("request_close_()", "process_close_()",
                                    "close_session_work(void *arg)", "destroy(void *ptr)",
                                    "send_buffer_work(void *arg)", "process_buffer_()"))
    safe_to_delete = method(header, "bool safe_to_delete_() const")
    harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "esp_http_server.h"

#define ESP_LOGW(...) ((void) 0)
#define ESP_LOGD(tag, format, fd) ((void) (fd))
#define ESP_LOGV(tag, format, sent, remaining, offset, size) ((void) (remaining))
constexpr int SHUT_RDWR = 2;
constexpr int HTTPD_SOCK_ERR_TIMEOUT = -1, HTTPD_SOCK_ERR_FAIL = -2, HTTPD_SOCK_ERR_INVALID = -3;
struct Clock {
  uint32_t now{100};
  uint32_t get_loop_component_start_time() const { return now; }
} App;
struct FakeHTTPD {
  int queue_result{ESP_OK}, queue_calls{0}, shutdown_calls{0};
  void (*queued)(void *){nullptr};
  void *argument{nullptr}, *session{nullptr};
  int fd{7};
  int send_calls{0}, send_result{2};
  std::string sent;
} httpd;
esp_err_t httpd_queue_work(httpd_handle_t, void (*work)(void *), void *arg) {
  ++httpd.queue_calls;
  if (httpd.queue_result != ESP_OK) return httpd.queue_result;
  assert(httpd.queued == nullptr);
  httpd.queued = work;
  httpd.argument = arg;
  return ESP_OK;
}
void *httpd_sess_get_ctx(httpd_handle_t, int fd) {
  assert(fd == httpd.fd);
  return httpd.session;
}
int httpd_socket_send(httpd_handle_t, int fd, const char *data, size_t length, int flags) {
  assert(fd == httpd.fd && flags == 0);
  ++httpd.send_calls;
  httpd.sent.assign(data, length);
  return httpd.send_result;
}
int shutdown(int fd, int how) {
  assert(fd == httpd.fd && how == SHUT_RDWR);
  ++httpd.shutdown_calls;
  return 0;
}
void deliver() {
  auto callback = httpd.queued;
  auto argument = httpd.argument;
  assert(callback != nullptr);
  httpd.queued = nullptr;
  callback(argument);
}
class AsyncEventSourceResponse {
 public:
  void request_close_();
  void process_close_();
  void process_buffer_();
  static void close_session_work(void *arg);
  static void send_buffer_work(void *arg);
  static void destroy(void *ptr);
  SAFE_TO_DELETE
  httpd_handle_t hd_{nullptr};
  std::atomic<int> fd_{7};
  std::vector<int> deferred_queue_{1, 2};
  std::string event_buffer_{"pending"};
  size_t event_bytes_sent_{2};
  uint32_t next_close_attempt_ms_{0};
  bool close_requested_{false}, close_retry_warning_logged_{false};
  std::atomic<bool> close_work_queued_{false};
  uint32_t send_failure_started_ms_{0}, next_send_attempt_ms_{0};
  uint32_t last_send_dispatch_loop_ms_{0};
  bool send_result_pending_{false};
  int send_result_{0};
  std::atomic<bool> send_work_queued_{false};
  PRODUCTION_CONSTANTS
};
PRODUCTION_METHODS

void reset() { httpd = {}; App.now = 100; }
void queue_failure_retry() {
  reset();
  AsyncEventSourceResponse response;
  httpd.queue_result = -1;
  response.request_close_();
  assert(httpd.queue_calls == 1 && !response.close_work_queued_);
  assert(response.close_retry_warning_logged_ && !response.safe_to_delete_());
  assert(response.deferred_queue_.empty() && response.event_buffer_.empty());
  assert(response.event_bytes_sent_ == 0);
  App.now += 249;
  response.process_close_();
  assert(httpd.queue_calls == 1);
  ++App.now;
  httpd.queue_result = ESP_OK;
  response.process_close_();
  assert(httpd.queue_calls == 2 && response.close_work_queued_);
  httpd.session = &response;
  deliver();
  assert(httpd.shutdown_calls == 1);
  response.request_close_();
  assert(httpd.queue_calls == 2);
  App.now += 1000;
  response.request_close_();
  assert(httpd.queue_calls == 3);
  deliver();
}
void delayed_callback_destroy_first() {
  reset();
  AsyncEventSourceResponse response;
  response.request_close_();
  App.now += 3600000;
  response.process_close_();
  assert(httpd.queue_calls == 1 && !response.safe_to_delete_());
  AsyncEventSourceResponse::destroy(&response);
  assert(response.fd_ == 0 && !response.safe_to_delete_());
  deliver();
  assert(httpd.shutdown_calls == 0 && response.safe_to_delete_());
}
void reused_fd_is_not_sent_or_closed() {
  reset();
  AsyncEventSourceResponse response, replacement;
  httpd.session = &replacement;
  response.process_buffer_();
  deliver();
  assert(httpd.send_calls == 0);
  response.process_buffer_();
  deliver();
  assert(httpd.shutdown_calls == 0 && !response.safe_to_delete_());
  AsyncEventSourceResponse::destroy(&response);
  assert(response.safe_to_delete_());
}
void pending_send_keeps_buffer_alive() {
  reset();
  AsyncEventSourceResponse response;
  httpd.session = &response;
  response.process_buffer_();
  App.now += 3600000;
  response.process_buffer_();
  response.request_close_();
  assert(httpd.queue_calls == 1 && response.event_buffer_ == "pending" && response.event_bytes_sent_ == 2);
  AsyncEventSourceResponse::destroy(&response);
  assert(!response.safe_to_delete_());
  deliver();
  response.process_close_();
  assert(httpd.send_calls == 0 && response.event_buffer_.empty() && response.safe_to_delete_());
}
void send_queue_retry_and_partial_progress() {
  reset();
  AsyncEventSourceResponse response;
  httpd.session = &response;
  httpd.queue_result = -1;
  response.process_buffer_();
  assert(!response.send_work_queued_ && !response.send_result_pending_ && response.event_buffer_ == "pending");
  App.now += 249;
  response.process_buffer_();
  assert(httpd.queue_calls == 1);
  ++App.now;
  httpd.queue_result = ESP_OK;
  response.process_buffer_();
  deliver();
  assert(httpd.sent == "nding" && response.event_bytes_sent_ == 2);
  response.process_buffer_();
  assert(response.event_bytes_sent_ == 4 && response.send_failure_started_ms_ == 0);
  response.process_buffer_();
  assert(httpd.queue_calls == 2 && httpd.queued == nullptr);  // Fast ACK cannot dispatch twice in this pass.
  ++App.now;
  response.process_buffer_();
  httpd.send_result = 3;
  deliver();
  assert(httpd.sent == "ing");
  response.process_buffer_();
  assert(response.event_buffer_.empty() && response.event_bytes_sent_ == 0);
}
int native_result = ESP_OK, native_calls = 0;
httpd_config_t captured{};
extern "C" esp_err_t __real_httpd_start(httpd_handle_t *handle, const httpd_config_t *config) {
  ++native_calls;
  if (handle == nullptr || config == nullptr) return ESP_ERR_INVALID_ARG;
  captured = *config;
  return native_result;
}
#include "WRAPPER_SOURCE"
void wrapper_preserves_contract() {
  httpd_handle_t handle{};
  httpd_config_t config{4352, 5, 80, 42};
  native_result = ESP_ERR_NO_MEM;
  assert(__wrap_httpd_start(&handle, &config) == ESP_ERR_NO_MEM);
  assert(captured.stack_size == 8192 && config.stack_size == 4352);
  assert(captured.priority == 5 && captured.port == 80 && captured.capabilities == 42);
  config.stack_size = 16384;
  native_result = ESP_OK;
  assert(__wrap_httpd_start(&handle, &config) == ESP_OK && captured.stack_size == 16384);
  const int before = native_calls;
  assert(__wrap_httpd_start(nullptr, &config) == ESP_ERR_INVALID_ARG);
  assert(__wrap_httpd_start(&handle, nullptr) == ESP_ERR_INVALID_ARG);
  assert(native_calls == before + 2);
}
int main() {
  queue_failure_retry();
  delayed_callback_destroy_first();
  reused_fd_is_not_sent_or_closed();
  pending_send_keeps_buffer_alive();
  send_queue_retry_and_partial_progress();
  wrapper_preserves_contract();
  std::puts("SSE lifecycle failure injection and HTTPD stack wrapper: PASS");
}
'''
    harness = harness.replace("SAFE_TO_DELETE", safe_to_delete)
    constants = re.findall(r"  static constexpr uint32_t (?:CLOSE|SEND)_\w+ = \d+;", header)
    assert len(constants) == 4, "Review changed production send/close constants"
    harness = harness.replace("PRODUCTION_CONSTANTS", "\n".join(constants))
    harness = harness.replace("PRODUCTION_METHODS", bodies)
    harness = harness.replace("WRAPPER_SOURCE", str(ROOT / "components/quatt_odu_simulator/httpd_stack.cpp"))
    with tempfile.TemporaryDirectory(prefix="simulator-sse-test-") as directory:
        target = Path(directory)
        (target / "esphome/core").mkdir(parents=True)
        (target / "esphome/core/defines.h").write_text("#define USE_WEBSERVER\n")
        (target / "esp_http_server.h").write_text(
            "#pragma once\n#include <cstddef>\nusing esp_err_t = int;\n"
            "using httpd_handle_t = void *;\nconstexpr int ESP_OK = 0;\n"
            "constexpr int ESP_ERR_INVALID_ARG = 1, ESP_ERR_NO_MEM = 2;\n"
            "struct httpd_config_t { size_t stack_size; int priority, port, capabilities; };\n")
        source = target / "lifecycle.cpp"
        source.write_text(harness)
        binary = target / "lifecycle"
        subprocess.run([*shlex.split(os.environ.get("CXX", "c++")),
                        "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(target), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
