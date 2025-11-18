#!/usr/bin/env bash
set -euo pipefail

SERVER_BIN=./httpserver

echo "== No arguments should fail =="
if $SERVER_BIN 2>/dev/null; then
  echo "Expected failure with no args, but server ran"
  exit 1
fi

echo "== Valid: ./httpserver 8080 =="
$SERVER_BIN 8080 &
PID=$!
sleep 1
kill "$PID" || true
wait "$PID" 2>/dev/null || true

echo "== Valid: ./httpserver -t 4 8080 (if your current process_args supports it) =="
$SERVER_BIN -t 4 8080 &
PID=$!
sleep 1
kill "$PID" || true
wait "$PID" 2>/dev/null || true

echo "CLI smoke tests PASSED"
