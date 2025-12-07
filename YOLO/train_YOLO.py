# aa_gpu_fix.py
# GPU training fallback: disable AMP + force FP32 + reduce batch/workers
from ultralytics import YOLO
import torch, os, zipfile, subprocess, sys

def run_cmd(cmd):
    try:
        out = subprocess.check_output(cmd, shell=True, stderr=subprocess.STDOUT, universal_newlines=True)
        return out.strip()
    except Exception as e:
        return f"Error running `{cmd}`: {e}"

print("=== Environment check ===")
print("Python:", sys.version.splitlines()[0])
print("Torch:", run_cmd("python -c \"import torch; print(torch.__version__)\""))
print("CUDA available:", torch.cuda.is_available())
if torch.cuda.is_available():
    print(run_cmd("nvidia-smi"))

# Paths
dataset_zip = "PHEROTRAP.v9i.yolov11.zip"
extract_dir = "datasets/PHEROTRAP"

# Extract dataset (if needed)
if not os.path.exists(extract_dir):
    print("📦 Extracting dataset...")
    with zipfile.ZipFile(dataset_zip, 'r') as z:
        z.extractall(extract_dir)
    print("✅ Extracted to:", extract_dir)
else:
    print("✅ Dataset already present, skipping extract.")

# Load base model
print("📥 Loading YOLOv11 base model (yolo11n.pt)...")
model = YOLO("yolo11m.pt")

# Training config tweaks to avoid cublas/amp issues
train_kwargs = dict(
    data=f"{extract_dir}/data.yaml",
    epochs=100,
    imgsz=640,
    batch=4,                         # lower batch to reduce memory pressure
    device=0 if torch.cuda.is_available() else 'cpu',
    project="runs/train",
    name="yolo11pherox",
    exist_ok=True,
    workers=0,                       # set 0 to avoid multiprocessing spawn issues
    amp=False,                       # DISABLE Automatic Mixed Precision (fix cublas amp error)
    half=False,                      # FORCE FP32 computations (do not use .half())
)

print("\n⚙️ Training settings (applied):")
for k, v in train_kwargs.items():
    print(f"  {k}: {v}")

print("\n🚀 Starting training (this may still use GPU but WITHOUT AMP)...\n")
model.train(**train_kwargs)

print("\n✅ Training finished (or exited). Check runs/train/yolo11_pherotrap_gpu_fix")
