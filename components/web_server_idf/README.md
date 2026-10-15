# ESPHome 2026.8.0 SSE lifecycle backport

Base: ESPHome **2026.8.0**, `esphome/components/web_server_idf`, copied from the
installed 2026.8.0 distribution. Copyright ESPHome. The included [LICENSE](LICENSE)
contains upstream's GPLv3 runtime and MIT Python licenses.

The lifecycle changes are selectively backported from ESPHome merge commit
`9309cf96b9710ad81d3e01a07a6f76637ba117a1`
([ESPHome #17800](https://github.com/esphome/esphome/pull/17800)).
The Python configuration and `multipart.cpp`, `multipart.h`, `utils.cpp` and
`utils.h` remain byte-identical to the 2026.8.0 distribution.

Only SSE send/close ownership, lifetime and stalled-send handling change:

- A stream without send progress for 20 seconds requests closure; failed or
  zero-byte sends also request closure.
- Normal sends also run as HTTPD work with a session-context identity check.
  One pending send pins the response and its immutable buffer; its result returns
  to the main loop through release/acquire synchronization. Queue failures retain
  the buffer and retry after 250 ms under the same 20-second no-progress deadline.
  Initial sorting groups retry in order before entity enumeration, so a pending
  asynchronous ping cannot discard their metadata. Each session dispatches at
  most one send per component-dispatch timestamp; pending buffers retain
  deferred generators without repeatedly serializing them during an update burst.
- The main loop queues close work for HTTPD. HTTPD verifies that the socket's
  session context still identifies this response before shutting down the socket.
- The response remains alive until HTTPD's destroy callback confirms closure
  and any queued send/close callback has finished all accesses.
- Queue failures retry after 250 ms. A successful queued callback is never
  assumed finished merely because time passed; no timeout releases its lifetime
  pin. Further close confirmation attempts use a 1-second interval.
- Closing streams stop generating events and clear pending buffers after the
  last queued send finishes.

The 2026.8.0 `std::string` URL constructor/storage, deprecated request `url()`
API and `ComponentIterator::advance()` remain intact. The newer `StringRef`
constructor and `try_advance()` API are deliberately not imported. This is not
an ESPHome version upgrade or a backport of unrelated web features.

`packages/system_diagnostics.yaml` selects this local component for the combined
simulator. Its `path: components` resolves against the top-level configuration
folder, as do the existing simulator component includes. The standalone boiler
configuration does not include this override.

The existing HTTPD stack wrapper remains separate: it raises the task's minimum
stack allocation to 8192 bytes and does not change task lifetime or allocation
capabilities. This backport retains HTTPD's normal startup and shutdown paths.

The observed native "Closing stuck EventSource" path can leave HTTPD referencing
an object already deleted by the main loop. This fixes that source-level lifecycle
defect. A recorded WiFi `ppTask` panic after stalled-stream warnings does not prove
that this defect caused that panic. Validate slow/non-reading clients, abrupt
closure, reconnects and queue failures with normal controller polling; compare
HTTP/SSE recovery, internal free/minimum/largest-block heap and stack watermarks.
Remove the override after validating an ESPHome release with equivalent fixes.
