#!/bin/bash
chmod +x "$0"

PROJECT_DIR="/home/adjira/esp_server"
VENV_DIR="$PROJECT_DIR/venv"
PYTHON_FILE="$PROJECT_DIR/kalyol.py"

cd "$PROJECT_DIR" || exit 1

if [ -f "$VENV_DIR/bin/activate" ]; then
    echo "🔹 Mengaktifkan virtual environment..."
    source "$VENV_DIR/bin/activate"
else
    echo "❌ Virtual environment tidak ditemukan di $VENV_DIR"
    exit 1
fi

# 🔧 Fix untuk Qt di Wayland
export QT_QPA_PLATFORM=xcb

echo "🚀 Menjalankan $PYTHON_FILE..."
python "$PYTHON_FILE"
