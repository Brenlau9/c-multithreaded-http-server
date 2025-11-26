#!/usr/bin/env bash
set -euo pipefail

SERVER_BIN=./httpserver
PORT=8082

echo "== [concurrency] Cleaning up test files =="
rm -f shared.txt somefile

echo "hello world" > somefile

echo "== [concurrency] Starting server with 4 threads on port ${PORT} =="
${SERVER_BIN} -t 4 "${PORT}" &
SERVER_PID=$!

cleanup() {
  echo "== [concurrency] Cleaning up server (PID: ${SERVER_PID}) =="
  kill "${SERVER_PID}" 2>/dev/null || true
  wait "${SERVER_PID}" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1

###############################################
# 1. Parallel GETs with unique Request-Id     #
###############################################
echo "== [concurrency] Testing parallel GETs on /somefile =="

pids=()
for i in $(seq 1 20); do
  curl --max-time 5 -s \
    -H "Request-Id: ${i}" \
    "http://127.0.0.1:${PORT}/somefile" > /dev/null &
  pids+=($!)
done

# Wait only for curl PIDs, not the server
for pid in "${pids[@]}"; do
  wait "$pid"
done

echo "== [concurrency] Parallel GETs completed =="

####################################################
# 2. Parallel GET + PUT on /shared.txt with IDs    #
####################################################
echo "== [concurrency] Testing parallel GET/PUT with Request-Id =="

pids=()

# Fire many PUTs (unique IDs)
for i in $(seq 1 50); do
  body="version-$i"
  echo "${body}" | curl --max-time 5 -s -X PUT \
    -H "Content-Length: ${#body}" \
    -H "Request-Id: ${i}" \
    --data-binary "@-" \
    "http://127.0.0.1:${PORT}/shared.txt" > /dev/null &
  pids+=($!)
done

# Fire many GETs at same time (unique IDs)
for i in $(seq 1 50); do
  curl --max-time 5 -s \
    -H "Request-Id: $((1000 + i))" \
    "http://127.0.0.1:${PORT}/shared.txt" > /dev/null &
  pids+=($!)
done

# Again, wait only for curl PIDs
for pid in "${pids[@]}"; do
  wait "$pid"
done

echo "== [concurrency] Parallel GET/PUT operations completed =="

####################################################
# 3. Final content check                           #
####################################################
if [ ! -f shared.txt ]; then
  echo "ERROR: shared.txt not created!"
  exit 1
fi

final=$(cat shared.txt)
echo "Final shared.txt content: '$final'"

case "$final" in
  version-*)
    echo "== [concurrency] Final content valid. Test PASSED =="
    ;;
  *)
    echo "ERROR: Unexpected content: '$final'"
    exit 1
    ;;
esac
