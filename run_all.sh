#!/usr/bin/env bash
set -euo pipefail

# ----------------- CONFIG -----------------
GIST_ID="XXX"
GITHUB_TOKEN="XXX"  # ganti dengan tokenmu sendiri
GIST_FILE="esp_server_url.txt"
SERVER_PORT=5000
CLOUDFLARE_TIMEOUT=30
# ------------------------------------------

cd "$(dirname "$0")"

err() { echo "ERROR: $*" >&2; exit 1; }

# 1) pastikan python ada
command -v python >/dev/null 2>&1 || command -v python3 >/dev/null 2>&1 || err "Python tidak ditemukan."

PYTHON_BIN="$(command -v python || command -v python3)"

# 2) buat venv kalau belum ada
if [ ! -x "./venv/bin/python" ]; then
  echo "[1/8] Membuat virtualenv..."
  "$PYTHON_BIN" -m venv venv || err "Gagal membuat venv"
else
  echo "[1/8] Virtualenv ditemukan."
fi

VENV_PY="./venv/bin/python"

# 3) install dependency
echo "[2/8] Menginstall dependency..."
"$VENV_PY" -m pip install --upgrade pip >/dev/null
"$VENV_PY" -m pip install flask requests >/dev/null

# 4) pastikan cloudflared ada
command -v cloudflared >/dev/null 2>&1 || err "cloudflared tidak ditemukan di PATH."

# 5) jalankan Flask server
echo "[3/8] Menjalankan Flask server..."
LOG_FLASK="$(mktemp -t flask_log.XXXXXX)"
"$VENV_PY" server.py >"$LOG_FLASK" 2>&1 &
FLASK_PID=$!
sleep 1
echo "[+] Flask PID: $FLASK_PID (log: $LOG_FLASK)"

# 6) jalankan cloudflared
echo "[4/8] Menjalankan cloudflared quick tunnel..."
LOG_CF="$(mktemp -t cf_log.XXXXXX)"
cloudflared tunnel --url "http://localhost:${SERVER_PORT}" >"$LOG_CF" 2>&1 &
CLOUDFLARE_PID=$!
echo "[+] cloudflared PID: $CLOUDFLARE_PID (log: $LOG_CF)"

# 7) tunggu URL
echo "[5/8] Menunggu URL trycloudflare..."
URL=""
START=$(date +%s)
while :; do
  if grep -Eo "https://[a-z0-9-]+\.trycloudflare\.com" "$LOG_CF" | head -n1 | read -r line; then
    URL=$(grep -Eo "https://[a-z0-9-]+\.trycloudflare\.com" "$LOG_CF" | head -n1)
    break
  fi
  now=$(date +%s)
  if [ $((now - START)) -ge "$CLOUDFLARE_TIMEOUT" ]; then
    echo "⏱ Timeout menunggu URL cloudflared."
    break
  fi
  sleep 0.2
done

if [ -z "$URL" ]; then
  echo "❌ Gagal memperoleh URL cloudflared. Periksa $LOG_CF"
  kill "$CLOUDFLARE_PID" 2>/dev/null || true
  kill "$FLASK_PID" 2>/dev/null || true
  exit 1
fi
echo "[+] URL cloudflared: $URL"

# 8) update Gist
echo "[6/8] Mengupdate Gist..."
PAYLOAD="{\"files\": {\"${GIST_FILE}\": {\"content\": \"${URL}\"}}}"
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X PATCH \
  -H "Authorization: token ${GITHUB_TOKEN}" \
  -H "Accept: application/vnd.github.v3+json" \
  -d "$PAYLOAD" \
  "https://api.github.com/gists/${GIST_ID}" || true)

if [ "$HTTP_STATUS" = "200" ]; then
  echo "[+] Gist berhasil diperbarui."
else
  echo "⚠️ Gagal update Gist (HTTP $HTTP_STATUS)."
fi

echo
echo "================ READY ================"
echo "Server URL (public): $URL"
echo "Flask log: $LOG_FLASK"
echo "cloudflared log: $LOG_CF"
echo "Tekan Ctrl+C untuk berhenti."
echo "======================================"

cleanup() {
  echo
  echo "[*] Cleanup: menghentikan Flask & cloudflared..."
  kill "$CLOUDFLARE_PID" 2>/dev/null || true
  kill "$FLASK_PID" 2>/dev/null || true
  exit 0
}
trap cleanup INT TERM

# 9) stream log
while true; do
  tail -n +1 -f "$LOG_CF" "$LOG_FLASK" 2>/dev/null | sed -u 's/^/[LOG] /'
  sleep 1
done
