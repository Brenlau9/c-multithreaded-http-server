#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${ROOT_DIR}/httpserver"

PORT="${PORT:-8083}"
THREADS="${THREADS:-4}"
VUS="${VUS:-50}"
DURATION="${DURATION:-15s}"
FILE_NAME="${FILE_NAME:-benchmark-target.txt}"
FILE_SIZE_BYTES="${FILE_SIZE_BYTES:-65536}"
K6_SUMMARY_TREND_STATS="${K6_SUMMARY_TREND_STATS:-avg,min,med,p(90),p(95),p(99),max}"

if ! command -v k6 >/dev/null 2>&1; then
  echo "k6 is required but was not found in PATH." >&2
  echo "Install it from https://grafana.com/docs/k6/latest/set-up/install-k6/ and rerun." >&2
  exit 1
fi

if [ ! -x "${SERVER_BIN}" ]; then
  echo "Building ${SERVER_BIN}..." >&2
  make -C "${ROOT_DIR}" httpserver >/dev/null
fi

if ! [[ "${PORT}" =~ ^[0-9]+$ ]] || [ "${PORT}" -lt 1 ] || [ "${PORT}" -gt 65535 ]; then
  echo "PORT must be an integer between 1 and 65535." >&2
  exit 1
fi

if ! [[ "${THREADS}" =~ ^[0-9]+$ ]] || [ "${THREADS}" -lt 1 ]; then
  echo "THREADS must be a positive integer." >&2
  exit 1
fi

if ! [[ "${VUS}" =~ ^[0-9]+$ ]] || [ "${VUS}" -lt 1 ]; then
  echo "VUS must be a positive integer." >&2
  exit 1
fi

if ! [[ "${FILE_SIZE_BYTES}" =~ ^[0-9]+$ ]] || [ "${FILE_SIZE_BYTES}" -lt 1 ]; then
  echo "FILE_SIZE_BYTES must be a positive integer." >&2
  exit 1
fi

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/httpserver-k6.XXXXXX")"
SERVER_LOG="${TMP_DIR}/server.log"
SERVER_PID=""

cleanup() {
  if [ -n "${SERVER_PID}" ]; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi

  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

(
  cd "${TMP_DIR}"
  exec "${SERVER_BIN}" -t "${THREADS}" "${PORT}" >"${SERVER_LOG}" 2>&1
) &
SERVER_PID="$!"

BASE_URL="http://127.0.0.1:${PORT}"

for _ in $(seq 1 50); do
  status="$(curl -s -o /dev/null -w '%{http_code}' "${BASE_URL}/__startup_probe__" || true)"
  if [ "${status}" = "404" ]; then
    break
  fi
  sleep 0.1
done

status="$(curl -s -o /dev/null -w '%{http_code}' "${BASE_URL}/__startup_probe__" || true)"
if [ "${status}" != "404" ]; then
  echo "Server did not become ready on ${BASE_URL}." >&2
  echo "Server log:" >&2
  sed -n '1,120p' "${SERVER_LOG}" >&2 || true
  exit 1
fi

echo "Running k6 benchmark against ${BASE_URL}/${FILE_NAME}"
echo "threads=${THREADS} vus=${VUS} duration=${DURATION} file_size_bytes=${FILE_SIZE_BYTES}"

BASE_URL="${BASE_URL}" \
FILE_NAME="${FILE_NAME}" \
FILE_SIZE_BYTES="${FILE_SIZE_BYTES}" \
VUS="${VUS}" \
DURATION="${DURATION}" \
k6 run - <<'EOF'
import http from 'k6/http';
import { check, fail } from 'k6';

const baseUrl = __ENV.BASE_URL;
const fileName = __ENV.FILE_NAME;
const fileSizeBytes = Number(__ENV.FILE_SIZE_BYTES || '65536');
const vus = Number(__ENV.VUS || '50');
const duration = __ENV.DURATION || '15s';

export const options = {
  vus,
  duration,
  thresholds: {
    http_req_failed: ['rate<0.01'],
    http_req_duration: ['p(95)<250'],
    checks: ['rate==1.0'],
  },
};

function buildBody(size) {
  return 'x'.repeat(size);
}

export function setup() {
  const url = `${baseUrl}/${fileName}`;
  const body = buildBody(fileSizeBytes);
  const response = http.put(url, body, {
    headers: {
      'Content-Length': String(body.length),
      'Request-Id': 'benchmark-setup',
    },
    tags: {
      phase: 'setup',
    },
  });

  const ok = check(response, {
    'setup PUT succeeded': (res) => res.status === 200 || res.status === 201,
  });

  if (!ok) {
    fail(`setup failed with status ${response.status}`);
  }

  return {
    expectedBodyLength: body.length,
    url,
  };
}

export default function (data) {
  const response = http.get(data.url, {
    headers: {
      'Request-Id': `benchmark-get-${__VU}-${__ITER}`,
    },
    tags: {
      endpoint: fileName,
      method: 'GET',
      phase: 'benchmark',
    },
  });

  check(response, {
    'GET returned 200': (res) => res.status === 200,
    'GET body length matched setup file': (res) => res.body.length === data.expectedBodyLength,
  });
}

export function teardown() {
  // Filesystem cleanup happens in the wrapper script by deleting the temp server directory.
}
EOF
