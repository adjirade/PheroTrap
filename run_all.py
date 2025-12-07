import subprocess
import requests
import time
import re
import os

# ========== KONFIGURASI ==========
GIST_ID = "XXX"
GITHUB_TOKEN = "XXX"  # ganti nanti
# =================================

def run_flask():
    return subprocess.Popen(["python", "server.py"])

def run_cloudflared():
    process = subprocess.Popen(
        ["cloudflared", "tunnel", "--url", "http://localhost:5000"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )
    url = None
    for line in process.stdout:
        print(line.strip())
        match = re.search(r"https://[a-z0-9\-]+\.trycloudflare\.com", line)
        if match:
            url = match.group(0)
            break
    return process, url

def update_gist(url):
    print(f"\n[+] Mengupdate Gist dengan URL: {url}")
    gist_api = f"https://api.github.com/gists/{GIST_ID}"
    headers = {
        "Authorization": f"token {GITHUB_TOKEN}"
    }
    data = {
        "files": {
            "esp_server_url.txt": {
                "content": url
            }
        }
    }
    response = requests.patch(gist_api, json=data, headers=headers)
    if response.status_code == 200:
        print("[+] Gist berhasil diperbarui.")
    else:
        print(f"[!] Gagal update Gist: {response.status_code} - {response.text}")

if __name__ == "__main__":
    print("[*] Menjalankan Flask server...")
    flask_proc = run_flask()
    time.sleep(2)

    print("[*] Menjalankan Cloudflared...")
    cloud_proc, url = run_cloudflared()

    if url:
        update_gist(url)
        print(f"[+] URL aktif: {url}")
    else:
        print("[!] Gagal mendapatkan URL Cloudflared")

    print("\nServer aktif. Tekan CTRL+C untuk berhenti.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        flask_proc.terminate()
        cloud_proc.terminate()
        print("\n[+] Semua proses dihentikan.")
