from ultralytics import YOLO
import cv2
import numpy as np

# Load model hasil training
model = YOLO("C:/Users/Admin/Downloads/project/runs/train/yolo11pherox/weights/best.pt")

video_path = "video_uji.mp4"

# ===== KALMAN FILTER 1D UNTUK COUNTING =====
class KalmanFilter1D:
    def __init__(self, process_variance=1e-3, measurement_variance=4):
        self.x = 0.0  # nilai estimasi
        self.P = 1.0  # covariance
        self.Q = process_variance  # noise proses
        self.R = measurement_variance  # noise pengukuran
        self.initialized = False

    def update(self, measurement):
        if not self.initialized:
            self.x = measurement
            self.initialized = True

        # Prediksi
        self.P += self.Q

        # Kalman gain
        K = self.P / (self.P + self.R)

        # Update estimasi
        self.x = self.x + K * (measurement - self.x)

        # Update covariance
        self.P = (1 - K) * self.P

        return self.x

# ===== INISIALISASI =====
kf = KalmanFilter1D()
last_filtered_count = 0

cv2.namedWindow("Hasil Deteksi", cv2.WINDOW_NORMAL)
cv2.resizeWindow("Hasil Deteksi", 720, 1280)

for result in model.predict(source=video_path, stream=True, device=0, show=False):
    frame = result.plot()

    # Hitung jumlah objek (deteksi YOLO)
    boxes = result.boxes.xyxy.cpu().numpy() if result.boxes is not None else []
    raw_count = len(boxes)

    # Update Kalman Filter
    filtered_count = kf.update(raw_count)

    # Pastikan count tidak menurun secara signifikan
    stable_count = max(int(filtered_count), last_filtered_count)
    last_filtered_count = stable_count

    # Tampilkan di layar
    cv2.putText(frame, f"Raw count: {raw_count}", (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
    cv2.putText(frame, f"Filtered count: {stable_count}", (20, 80),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)

    frame = cv2.resize(frame, (720, 1280))
    cv2.imshow("Hasil Deteksi", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
