#!/bin/sh
set -eu

simulator_url="http://192.168.2.63"
mode=""

usage() {
  echo "Usage: $0 --mode parity|framing [--simulator URL]" >&2
  exit 2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --mode)
      mode="$2"
      shift 2
      ;;
    --simulator)
      simulator_url="${2%/}"
      shift 2
      ;;
    *) usage ;;
  esac
done

case "$mode" in
  parity)
    button_name="Inject M2 UART parity error"
    counter_name="M2 UART parity faults injected"
    ;;
  framing)
    button_name="Inject M2 UART framing error"
    counter_name="M2 UART framing faults injected"
    ;;
  *) usage ;;
esac

url_encode_spaces() {
  printf '%s' "$1" | sed 's/ /%20/g'
}

request() {
  curl -fsS --max-time 5 "$1"
}

post() {
  curl -fsS -X POST -d '' --max-time 5 "$1"
}

number_value() {
  request "$1" | sed -n 's/.*"value":\([0-9][0-9]*\).*/\1/p'
}

binary_value() {
  request "$1" | sed -n 's/.*"value":\(true\|false\).*/\1/p'
}

switch_url="$simulator_url/switch/$(url_encode_spaces 'M2 UART fault injection enabled')"
counter_url="$simulator_url/sensor/$(url_encode_spaces "$counter_name")"
restore_url="$simulator_url/sensor/$(url_encode_spaces 'M2 UART fault restore errors')"
active_url="$simulator_url/binary_sensor/$(url_encode_spaces 'M2 UART fault injection active')"
button_url="$simulator_url/button/$(url_encode_spaces "$button_name")/press"

cleanup() {
  post "$switch_url/turn_off" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

before_count="$(number_value "$counter_url")"
before_restore="$(number_value "$restore_url")"
[ -n "$before_count" ] && [ -n "$before_restore" ]

post "$switch_url/turn_on" >/dev/null
request "$switch_url" | grep -q '"value":true'
# The template-switch state is published before the simulator's 500 ms
# configuration interval applies it to the UART hub.  Wait across two ticks so
# the immediately following button press cannot be rejected as disabled.
sleep 1
post "$button_url" >/dev/null

attempt=0
while [ "$attempt" -lt 20 ]; do
  count="$(number_value "$counter_url")"
  restore="$(number_value "$restore_url")"
  active="$(binary_value "$active_url")"
  if [ "$count" = "$((before_count + 1))" ] && [ "$restore" = "$before_restore" ] && [ "$active" = false ]; then
    printf 'PASS M2 %s injected=%s restore_errors=%s\n' "$mode" "$count" "$restore"
    exit 0
  fi
  attempt=$((attempt + 1))
  sleep 1
done

echo "FAIL M2 $mode fault was not confirmed within 20 seconds" >&2
exit 1
