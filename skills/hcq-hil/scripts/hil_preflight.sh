#!/bin/sh
set -eu

simulator_url="http://192.168.2.63/"
controller_url="http://openquatt-test.local/"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --simulator)
      simulator_url="$2"
      shift 2
      ;;
    --controller)
      controller_url="$2"
      shift 2
      ;;
    *)
      echo "Usage: $0 [--simulator URL] [--controller URL]" >&2
      exit 2
      ;;
  esac
done

check_url() {
  label="$1"
  url="$2"
  status="$(curl -sS -o /dev/null -w '%{http_code}' --max-time 5 "$url")"
  if [ "$status" = "200" ]; then
    printf 'PASS %s %s HTTP %s\n' "$label" "$url" "$status"
  else
    printf 'FAIL %s %s HTTP %s\n' "$label" "$url" "$status" >&2
    return 1
  fi
}

check_url simulator "$simulator_url"
check_url controller "$controller_url"
