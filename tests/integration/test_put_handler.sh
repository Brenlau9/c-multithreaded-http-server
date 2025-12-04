#!/usr/bin/env bash
set -euo pipefail

SERVER_BIN=./httpserver
PORT=8082   # use a different port from other tests if you like

fail=0

echo "== Cleaning up old test artifacts =="
rm -f put_small_text put_small_binary put_large_binary put_empty_file
TMPDIR="$(mktemp -d)"

echo "Using temp dir for expected data: ${TMPDIR}"

echo "== Starting server on port ${PORT} =="
$SERVER_BIN $PORT &
SERVER_PID=$!

cleanup() {
  echo "== Cleaning up server (PID: ${SERVER_PID}) =="
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true

  echo "== Removing temp and test files =="
  rm -rf "${TMPDIR}"
  rm -f put_small_text put_small_binary put_large_binary put_empty_file
}
trap cleanup EXIT

# Give it a moment to start
sleep 1

# ---- Helper functions ----

assert_eq() {
  local expected="$1"
  local actual="$2"
  local msg="${3:-}"
  if [ "$expected" != "$actual" ]; then
    echo "Expected '$expected', got '$actual'. $msg"
    fail=1
  fi
}

assert_file_equals() {
  local actual="$1"
  local expected="$2"
  if ! cmp -s "$actual" "$expected"; then
    echo "File mismatch: $actual != $expected"
    fail=1
  fi
}

put_file() {
  local uri="$1"   # e.g. "put_small_text"
  local src="$2"   # path to source data
  curl --max-time 5 -s -o /tmp/put_body.txt -w "%{http_code}" \
    -X PUT \
    --data-binary @"${src}" \
    "http://localhost:${PORT}/${uri}"
}

put_empty_body() {
  local uri="$1"
  curl --max-time 5 -s -o /tmp/put_body_empty.txt -w "%{http_code}" \
    -X PUT \
    --data-binary "" \
    "http://localhost:${PORT}/${uri}"
}

echo "== Running PUT handler tests =="

# 1) Small text body: create (201) then overwrite (200)
echo "== Test 1: small text body (create & overwrite) =="

src1="${TMPDIR}/src_small_text.txt"
src1b="${TMPDIR}/src_small_text_overwrite.txt"
printf 'hello world' > "${src1}"
printf 'overwritten content' > "${src1b}"

status=$(put_file "put_small_text" "${src1}")
echo "Status 1a (create): $status"
assert_eq "201" "$status" "PUT should return 201 when creating new file"
assert_file_equals "put_small_text" "${src1}"

status=$(put_file "put_small_text" "${src1b}")
echo "Status 1b (overwrite): $status"
assert_eq "200" "$status" "PUT should return 200 when overwriting existing file"
assert_file_equals "put_small_text" "${src1b}"

# 2) Small binary body with embedded NULs
echo "== Test 2: small binary with embedded NULs =="

src2="${TMPDIR}/src_small_binary.bin"
# bytes: 00 01 02 'h' 'e' 'l' 'l' 'o' 00 'w' 'o' 'r' 'l' 'd' FF 10
printf '\x00\x01\x02hello\x00world\xff\x10' > "${src2}"

status=$(put_file "put_small_binary" "${src2}")
echo "Status 2: $status"
if [ "$status" -ne 200 ] && [ "$status" -ne 201 ]; then
  echo "Expected 200 or 201 for small binary PUT, got $status"
  fail=1
fi
assert_file_equals "put_small_binary" "${src2}"

# 3) Large binary body (> buffer size) to exercise pass_n_bytes path
echo "== Test 3: large binary body (> BUF_SIZE) =="

src3="${TMPDIR}/src_large_binary.bin"
# Generate ~8 KB of random data (> 2048) so it can't all be in the first read
head -c 8192 /dev/urandom > "${src3}"

status=$(put_file "put_large_binary" "${src3}")
echo "Status 3: $status"
if [ "$status" -ne 200 ] && [ "$status" -ne 201 ]; then
  echo "Expected 200 or 201 for large binary PUT, got $status"
  fail=1
fi
assert_file_equals "put_large_binary" "${src3}"

# 4) Empty body (Content-Length: 0) should truncate the file
echo "== Test 4: empty body (Content-Length: 0 truncation) =="

src4="${TMPDIR}/src_nonempty.txt"
printf 'initial contents' > "${src4}"

# First create non-empty file
status=$(put_file "put_empty_file" "${src4}")
echo "Status 4a (create non-empty): $status"
if [ "$status" -ne 200 ] && [ "$status" -ne 201 ]; then
  echo "Expected 200 or 201 for initial PUT, got $status"
  fail=1
fi
assert_file_equals "put_empty_file" "${src4}"

# Now send zero-length body → file should become empty
status=$(put_empty_body "put_empty_file")
echo "Status 4b (zero-length PUT): $status"
assert_eq "200" "$status" "Expected 200 when truncating with empty PUT"

if [ -s "put_empty_file" ]; then
  echo "Expected put_empty_file to be empty after zero-length PUT, but it is not"
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo "PUT handler integration tests FAILED"
  exit 1
fi

echo "PUT handler integration tests PASSED"
