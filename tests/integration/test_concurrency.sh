#!/usr/bin/env bash
set -euo pipefail

SERVER_BIN=./httpserver
PORT=8082

echo "== [concurrency] Cleaning up test files =="
rm -f shared.txt somefile

# Create a base file for simple concurrent GETs
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

# Give the server a moment to start
sleep 1

########################################
# 1. Simple parallel GETs on somefile  #
########################################
echo "== [concurrency] Testing parallel GETs on /somefile =="

# 20 parallel GETs using xargs -P for concurrency
seq 1 20 | xargs -n1 -P8 -I{} curl --max-time 5 -s \
  "http://127.0.0.1:${PORT}/somefile" > /dev/null

echo "== [concurrency] Parallel GETs on /somefile completed =="

########################################
# 2. Parallel GET + PUT on shared.txt  #
########################################
echo "== [concurrency] Testing parallel GETs and PUTs on /shared.txt =="

# Launch many PUT requests in the background
for i in $(seq 1 50); do
  body="version-${i}"
  # Send the body via stdin so Content-Length is correct
  echo "${body}" | curl --max-time 5 -s -X PUT \
    -H "Content-Length: ${#body}" \
    --data-binary "@-" \
    "http://127.0.0.1:${PORT}/shared.txt" > /dev/null &
done

# At the same time, launch many GET requests in the background
for i in $(seq 1 50); do
  curl --max-time 5 -s \
    "http://127.0.0.1:${PORT}/shared.txt" > /dev/null &
done

# Wait for all background curl jobs to finish
wait

echo "== [concurrency] Parallel GET/PUT operations completed =="

########################################
# 3. Check final state of shared.txt   #
########################################
echo "== [concurrency] Verifying final contents of shared.txt =="

if [ ! -f shared.txt ]; then
  echo "ERROR: shared.txt was not created by PUT requests."
  exit 1
fi

final_content=$(cat shared.txt)
echo "Final shared.txt content: '${final_content}'"

# We expect the final content to be one of the 'version-X' strings
case "${final_content}" in
  version-*)
    echo "== [concurrency] Final content looks valid (version-*). Concurrency test PASSED =="
    ;;
  *)
    echo "ERROR: Unexpected final content in shared.txt"
    exit 1
    ;;
esac
