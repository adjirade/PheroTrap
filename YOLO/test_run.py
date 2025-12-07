from ultralytics import YOLO
import cv2

# Ganti dengan path model hasil training kamu (.pt)
model = YOLO("C:/Users/Admin/Downloads/project/runs/train/yolo11_pherotrap_gpu_fix/weights/best.pt")

# Ganti dengan path video input kamu
video_path = "video_uji.mp4"

# Buat jendela dengan ukuran tetap
cv2.namedWindow("Hasil Deteksi", cv2.WINDOW_NORMAL)
cv2.resizeWindow("Hasil Deteksi", 720, 1280)

# Jalankan inferensi langsung frame-by-frame (stream)
for result in model.predict(source=video_path, stream=True, device=0, show=False):
    frame = result.plot()  # hasil frame dengan bounding box dan label
    
    # Resize frame ke 640x640
    resized_frame = cv2.resize(frame, (720, 1280))
    
    # Tampilkan frame
    cv2.imshow("Hasil Deteksi", resized_frame)
    
    # Tekan 'q' untuk keluar
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
