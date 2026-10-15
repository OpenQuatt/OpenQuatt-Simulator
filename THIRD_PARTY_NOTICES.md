# Third-party notices

## OpenTherm communication library

Files:

- `components/hcq_ot_boiler_simulator/OpenTherm.h`
- `components/hcq_ot_boiler_simulator/OpenTherm.cpp`

Original project: <https://github.com/ihormelnyk/OpenTherm>

Original copyright:

> Copyright 2018, Ihor Melnyk

The original library is licensed under the MIT License. The files included in
this project contain later ESP32-S3 ESP-IDF/RMT adaptations and retain the
original attribution in their headers.

## ESPHome native HTTP server

`components/web_server_idf/` contains ESPHome 2026.8.0 with a selective SSE
lifecycle backport from ESPHome commit
`9309cf96b9710ad81d3e01a07a6f76637ba117a1` (PR #17800).

Copyright (c) 2019 ESPHome. C++ runtime files use GPLv3; Python uses MIT.
The upstream license and exact backport scope are included in
`components/web_server_idf/LICENSE` and `components/web_server_idf/README.md`.
