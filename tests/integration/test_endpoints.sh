#!/usr/bin/env bash
set -euo pipefail

SERVER_BIN=./httpserver
PORT=8081

echo "== Cleaning up and preparing test files =="
rm -f somefile put_testfile
echo "hello world" > somefile

echo "== Starting server on port ${PORT} =="
$SERVER_BIN $PORT &
SERVER_PID=$!

cleanup() {
  echo "== Cleaning up server (PID: ${SERVER_PID}) =="
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Give it a moment to start
sleep 1

fail=0

echo "== Testing GET existing file =="
status=$(curl --max-time 5 -s -o /tmp/body1.txt -w "%{http_code}" "http://localhost:${PORT}/somefile")
echo "Status for /somefile: $status"
if [ "$status" -ne 200 ]; then
  echo "Expected 200 for /somefile, got $status"
  fail=1
else
  body=$(cat /tmp/body1.txt)
  echo "Body for /somefile: '$body'"
  if [ "$body" != "hello world" ]; then
    echo "Unexpected body for /somefile: '$body'"
    fail=1
  fi
fi

echo "== Testing GET nonexistent file =="
status=$(curl --max-time 5 -s -o /tmp/body2.txt -w "%{http_code}" "http://localhost:${PORT}/does-not-exist")
echo "Status for /does-not-exist: $status"
if [ "$status" -ne 404 ]; then
  echo "Expected 404 for /does-not-exist, got $status"
  fail=1
fi

echo "== Testing PUT new file =="
status=$(curl --max-time 5 -s -o /tmp/body3.txt -w "%{http_code}" \
  -X PUT \
  -H "Content-Length: 11" \
  --data-binary "new content" \
  "http://localhost:${PORT}/put_testfile")

echo "Status for PUT /put_testfile: $status"
if [ "$status" -ne 201 ]; then
  echo "Expected 201 for PUT /put_testfile, got $status"
  fail=1
fi

echo "== Testing GET newly created file =="
status=$(curl --max-time 5 -s -o /tmp/body4.txt -w "%{http_code}" "http://localhost:${PORT}/put_testfile")
echo "Status for /put_testfile: $status"
if [ "$status" -ne 200 ]; then
  echo "Expected 200 for /put_testfile, got $status"
  fail=1
else
  body4=$(cat /tmp/body4.txt)
  echo "Body for /put_testfile: '$body4'"
  if [ "$body4" != "new content" ]; then
    echo "Unexpected body for /put_testfile: '$body4'"
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo "Integration tests FAILED"
  exit 1
fi

echo "Integration tests PASSED"
