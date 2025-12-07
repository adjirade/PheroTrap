from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)

UPLOAD_FOLDER = 'uploads'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

MAX_FILES = 1  # maksimal jumlah file di folder

def cleanup_old_files():
    """Hapus file lama jika lebih dari MAX_FILES"""
    files = [os.path.join(UPLOAD_FOLDER, f) for f in os.listdir(UPLOAD_FOLDER)]
    files = [f for f in files if os.path.isfile(f)]
    files.sort(key=os.path.getmtime, reverse=True)  # urut dari terbaru ke terlama

    if len(files) > MAX_FILES:
        for f in files[MAX_FILES:]:
            try:
                os.remove(f)
                print(f"[*] Hapus file lama: {os.path.basename(f)}")
            except Exception as e:
                print(f"[!] Gagal hapus {f}: {e}")

@app.route('/')
def index():
    return "Server aktif dan siap menerima gambar."

@app.route('/upload', methods=['POST'])
def upload_image():
    # ---- Terima file dari form-data ----
    if 'image' in request.files:
        image = request.files['image']
        filename = image.filename or datetime.now().strftime("esp_%Y%m%d_%H%M%S.jpg")
        path = os.path.join(UPLOAD_FOLDER, filename)
        image.save(path)
        print(f"[+] Gambar disimpan: {path}")
        cleanup_old_files()
        return jsonify({'success': True, 'filename': filename})

    # ---- Terima raw binary (image/jpeg langsung dari ESP32) ----
    elif request.data:
        filename = datetime.now().strftime("esp_raw_%Y%m%d_%H%M%S.jpg")
        path = os.path.join(UPLOAD_FOLDER, filename)
        with open(path, 'wb') as f:
            f.write(request.data)
        print(f"[+] Gambar disimpan: {path}")
        cleanup_old_files()
        return jsonify({'success': True, 'filename': filename})

    else:
        return jsonify({'error': 'Tidak ada data diterima'}), 400

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
