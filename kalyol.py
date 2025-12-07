import os
import time
import cv2
import numpy as np
import paho.mqtt.client as mqtt
from ultralytics import YOLO
from collections import deque
from pathlib import Path
import logging

# ==========================
# SETUP LOGGING
# ==========================
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# ==========================
# KONFIGURASI
# ==========================
class Config:
    # YOLO - OPTIMIZED PARAMETERS
    MODEL_PATH = "/home/adjira/esp_server/YOLO/runs/train/yolo11pherox/weights/best.pt"
    UPLOAD_DIR = "/home/adjira/esp_server/uploads/"
    CONFIDENCE_THRESHOLD = 0.4
    IOU_THRESHOLD = 0.5
    
    # YOLO Inference Optimization
    IMG_SIZE = 640
    AUGMENT = True
    AGNOSTIC_NMS = False
    MAX_DET = 500
    
    # MQTT
    BROKER = "XXX"
    PORT = 8883
    USERNAME = "XXX"
    PASSWORD = "XXX"
    TOPIC = "/pest"
    
    # Visualisasi
    WINDOW_WIDTH = 1280
    WINDOW_HEIGHT = 720
    GRAPH_WIDTH = 1280
    GRAPH_HEIGHT = 720
    BUFFER_SIZE = 100
    
    # Timing
    FRAME_DELAY = 1.0
    DISPLAY_DELAY = 50
    
    TEMPORAL_WINDOW = 5
    CONFIDENCE_BOOST = True

# ==========================
# ENHANCED KALMAN FILTER WITH OUTLIER REJECTION
# ==========================
class EnhancedKalmanFilter:
    """Kalman Filter dengan outlier rejection dan adaptive tuning"""
    def __init__(self, process_variance=1e-3, measurement_variance=1.5):
        self.x = 0.0
        self.P = 1.0
        self.Q = process_variance
        self.R = measurement_variance
        self.initialized = False
        self.innovation_history = deque(maxlen=15)
        self.prediction_history = deque(maxlen=10)
        
    def update(self, measurement):
        if not self.initialized:
            self.x = measurement
            self.initialized = True
            return self.x
        
        # Prediction
        prediction = self.x
        self.P += self.Q
        
        # Innovation (error)
        innovation = measurement - prediction
        self.innovation_history.append(abs(innovation))
        
        # ✅ OUTLIER REJECTION - Tolak measurement yang terlalu ekstrem
        if len(self.innovation_history) > 5:
            median_innovation = np.median(self.innovation_history)
            innovation_threshold = 3.0 * median_innovation  # 3-sigma rule
            
            if abs(innovation) > innovation_threshold and innovation_threshold > 1.0:
                logger.warning(f"⚠️  Outlier detected: {measurement} vs predicted {prediction:.1f}")
                # Gunakan prediksi saja, jangan update
                return self.x
        
        # Adaptive R based on recent innovations
        if len(self.innovation_history) > 8:
            avg_innovation = np.mean(self.innovation_history)
            self.R = max(0.8, min(3.0, avg_innovation * 1.5))
        
        # Update
        S = self.P + self.R
        K = self.P / S
        
        self.x = self.x + K * innovation
        self.P = (1 - K) * self.P
        
        self.prediction_history.append(self.x)
        
        return self.x

# ==========================
# TEMPORAL CONSISTENCY FILTER
# ==========================
class TemporalConsistencyFilter:
    """Filter untuk menjaga konsistensi temporal antar frame"""
    def __init__(self, window_size=5, outlier_threshold=0.4):
        self.window = deque(maxlen=window_size)
        self.outlier_threshold = outlier_threshold
    
    def update(self, value):
        self.window.append(value)
        
        if len(self.window) < 3:
            return value
        
        # ✅ Median filter untuk menghilangkan outlier
        median_val = np.median(self.window)
        
        # ✅ Weighted average: beri bobot lebih pada nilai tengah
        weights = np.array([0.1, 0.2, 0.4, 0.2, 0.1][-len(self.window):])
        weights = weights / weights.sum()
        weighted_avg = np.average(self.window, weights=weights)
        
        # Kombinasi median dan weighted average
        result = 0.6 * weighted_avg + 0.4 * median_val
        
        return result

# ==========================
# ENHANCED DETECTION MANAGER
# ==========================
class EnhancedDetectionManager:
    def __init__(self, model_path, config):
        self.model = YOLO(model_path)
        self.config = config
        self.kalman = EnhancedKalmanFilter()
        self.temporal = TemporalConsistencyFilter(window_size=config.TEMPORAL_WINDOW)
        
        # ✅ Track detection history untuk confidence boosting
        self.detection_history = deque(maxlen=20)
        
        logger.info(f"✅ Model loaded: {model_path}")
        logger.info(f"   Confidence threshold: {config.CONFIDENCE_THRESHOLD}")
        logger.info(f"   IOU threshold: {config.IOU_THRESHOLD}")
    
    def preprocess_image(self, image_path):
        """✅ Preprocessing untuk meningkatkan kualitas deteksi"""
        img = cv2.imread(image_path)
        if img is None:
            return None
        
        # 1. CLAHE untuk meningkatkan kontras
        lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
        l, a, b = cv2.split(lab)
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
        l = clahe.apply(l)
        enhanced = cv2.merge([l, a, b])
        enhanced = cv2.cvtColor(enhanced, cv2.COLOR_LAB2BGR)
        
        # 2. Slight denoising (hati-hati, jangan terlalu kuat)
        denoised = cv2.fastNlMeansDenoisingColored(enhanced, None, 5, 5, 7, 15)
        
        # 3. Slight sharpening
        kernel = np.array([[-1,-1,-1],
                          [-1, 9,-1],
                          [-1,-1,-1]])
        sharpened = cv2.filter2D(denoised, -1, kernel * 0.3)
        
        return sharpened
    
    def detect(self, image_path):
        """✅ Enhanced detection dengan preprocessing dan augmentation"""
        
        # Preprocess
        processed_img = self.preprocess_image(image_path)
        if processed_img is None:
            logger.error(f"Failed to load image: {image_path}")
            return None
        
        # Save temporary processed image
        temp_path = "/tmp/processed_frame.jpg"
        cv2.imwrite(temp_path, processed_img)
        
        # ✅ Run inference dengan parameter optimal
        results = self.model.predict(
            source=temp_path,
            device="cpu",
            conf=self.config.CONFIDENCE_THRESHOLD,
            iou=self.config.IOU_THRESHOLD,
            imgsz=self.config.IMG_SIZE,
            augment=self.config.AUGMENT,  # Test-time augmentation
            agnostic_nms=self.config.AGNOSTIC_NMS,
            max_det=self.config.MAX_DET,
            verbose=False,
            show=False
        )
        
        # Cleanup
        if os.path.exists(temp_path):
            os.remove(temp_path)
        
        return results[0]
    
    def get_filtered_count(self, raw_count):
        """✅ Triple filtering: Temporal → Kalman → Round"""
        
        # Stage 1: Temporal consistency
        temporal_filtered = self.temporal.update(raw_count)
        
        # Stage 2: Kalman filter
        kalman_filtered = self.kalman.update(temporal_filtered)
        
        # Stage 3: Smart rounding
        # Jika perbedaan dengan raw count kecil, percaya raw count
        diff = abs(kalman_filtered - raw_count)
        if diff < 1.5 and len(self.detection_history) > 5:
            # Cek apakah raw count konsisten dengan history
            recent_avg = np.mean(list(self.detection_history)[-5:])
            if abs(raw_count - recent_avg) < 3:
                final_count = int(round(raw_count))
            else:
                final_count = int(round(kalman_filtered))
        else:
            final_count = int(round(kalman_filtered))
        
        self.detection_history.append(final_count)
        
        return final_count

# ==========================
# MQTT MANAGER (Unchanged)
# ==========================
class MQTTManager:
    def __init__(self, broker, port, username, password):
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
        self.client.username_pw_set(username, password)
        self.client.tls_set()
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.connected = False

        try:
            self.client.connect(broker, port)
            self.client.loop_start()
            logger.info(f"🔌 Connecting to MQTT broker {broker}:{port} ...")
        except Exception as e:
            logger.error(f"MQTT connection failed: {e}")

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code == 0:
            self.connected = True
            logger.info("✅ MQTT connected successfully")
        else:
            self.connected = False
            logger.error(f"❌ MQTT connection failed (reason_code={reason_code})")

    def _on_disconnect(self, client, userdata, reason_code, properties=None):
        self.connected = False
        logger.warning(f"⚠️ MQTT disconnected (code: {reason_code})")

    def publish(self, topic, message):
        if self.connected:
            self.client.publish(topic, str(message))
            return True
        return False

# ==========================
# ENHANCED GRAPH RENDERER
# ==========================
class EnhancedGraphRenderer:
    def __init__(self, width, height, config):
        self.width = width
        self.height = height
        self.config = config
        self.margin_left = 80
        self.margin_right = 40
        self.margin_top = 60
        self.margin_bottom = 100
        self.plot_width = width - self.margin_left - self.margin_right
        self.plot_height = height - self.margin_top - self.margin_bottom
        
    def render(self, x_data, raw_data, filtered_data, temporal_data=None):
        """✅ Enhanced rendering dengan 3 lines"""
        img = np.ones((self.height, self.width, 3), dtype=np.uint8) * 250
        
        cv2.rectangle(img, 
                     (self.margin_left, self.margin_top),
                     (self.width - self.margin_right, self.height - self.margin_bottom),
                     (255, 255, 255), -1)
        
        if not raw_data or len(raw_data) < 2:
            return img
        
        max_val = max(max(raw_data), max(filtered_data), 1)
        if temporal_data:
            max_val = max(max_val, max(temporal_data))
        y_scale = self.plot_height / (max_val * 1.1)
        
        self._draw_grid(img, max_val)
        
        # Plot all data
        self._plot_line(img, x_data, raw_data, y_scale, (0, 0, 255), 2)  # Red: Raw
        if temporal_data:
            self._plot_line(img, x_data, temporal_data, y_scale, (255, 150, 0), 2)  # Orange: Temporal
        self._plot_line(img, x_data, filtered_data, y_scale, (0, 200, 0), 3)  # Green: Final
        
        self._draw_axes(img)
        self._draw_labels(img, max_val, len(x_data))
        self._draw_legend(img, has_temporal=temporal_data is not None)
        self._draw_statistics(img, raw_data, filtered_data)
        
        return img
    
    def _draw_grid(self, img, max_val):
        num_h_lines = 6
        for i in range(num_h_lines + 1):
            y = self.margin_top + int(i * self.plot_height / num_h_lines)
            cv2.line(img, (self.margin_left, y), 
                    (self.width - self.margin_right, y), (220, 220, 220), 1)
        
        num_v_lines = 10
        for i in range(num_v_lines + 1):
            x = self.margin_left + int(i * self.plot_width / num_v_lines)
            cv2.line(img, (x, self.margin_top), 
                    (x, self.height - self.margin_bottom), (220, 220, 220), 1)
    
    def _plot_line(self, img, x_data, y_data, y_scale, color, thickness):
        points = []
        for i, (x, y) in enumerate(zip(x_data, y_data)):
            px = self.margin_left + int(i * self.plot_width / (self.config.BUFFER_SIZE - 1))
            py = self.height - self.margin_bottom - int(y * y_scale)
            points.append((px, py))
        
        for i in range(1, len(points)):
            cv2.line(img, points[i-1], points[i], color, thickness, cv2.LINE_AA)
            
        for point in points:
            cv2.circle(img, point, 3, color, -1)
    
    def _draw_axes(self, img):
        cv2.line(img, (self.margin_left, self.margin_top),
                (self.margin_left, self.height - self.margin_bottom), (50, 50, 50), 2)
        cv2.line(img, (self.margin_left, self.height - self.margin_bottom),
                (self.width - self.margin_right, self.height - self.margin_bottom), (50, 50, 50), 2)
    
    def _draw_labels(self, img, max_val, num_points):
        cv2.putText(img, "WERENG COUNT (TRIPLE FILTERED)", (10, 35),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.8, (50, 50, 50), 2)
        
        num_labels = 6
        for i in range(num_labels + 1):
            val = int(max_val * (1 - i / num_labels))
            y = self.margin_top + int(i * self.plot_height / num_labels)
            cv2.putText(img, str(val), (10, y + 5),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.5, (50, 50, 50), 1)
        
        cv2.putText(img, "FRAME INDEX", (self.width // 2 - 80, self.height - 20),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.8, (50, 50, 50), 2)
        
        num_x_labels = 5
        buffer_size = self.config.BUFFER_SIZE
        for i in range(num_x_labels + 1):
            frame_num = max(0, num_points - buffer_size + int(buffer_size * i / num_x_labels))
            x = self.margin_left + int(i * self.plot_width / num_x_labels)
            cv2.putText(img, str(frame_num), (x - 15, self.height - self.margin_bottom + 25),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.5, (50, 50, 50), 1)
    
    def _draw_legend(self, img, has_temporal=False):
        legend_x = self.width - 320
        legend_y = self.margin_top + 20
        
        cv2.line(img, (legend_x, legend_y), (legend_x + 40, legend_y), (0, 0, 255), 2)
        cv2.putText(img, "Raw (YOLO)", (legend_x + 50, legend_y + 5),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.6, (50, 50, 50), 1)
        
        if has_temporal:
            cv2.line(img, (legend_x, legend_y + 30), (legend_x + 40, legend_y + 30), (255, 150, 0), 2)
            cv2.putText(img, "Temporal Filter", (legend_x + 50, legend_y + 35),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.6, (50, 50, 50), 1)
        
        cv2.line(img, (legend_x, legend_y + 60), (legend_x + 40, legend_y + 60), (0, 200, 0), 3)
        cv2.putText(img, "Final (Kalman)", (legend_x + 50, legend_y + 65),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.6, (50, 50, 50), 1)
    
    def _draw_statistics(self, img, raw_data, filtered_data):
        stats_y = self.height - self.margin_bottom + 55
        
        raw_mean = np.mean(raw_data)
        raw_std = np.std(raw_data)
        filtered_mean = np.mean(filtered_data)
        filtered_std = np.std(filtered_data)
        improvement = ((raw_std - filtered_std) / raw_std * 100) if raw_std > 0 else 0
        
        cv2.putText(img, f"Raw: miu={raw_mean:.1f}, sigma={raw_std:.2f}", 
                   (self.margin_left + 20, stats_y), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 200), 2)
        cv2.putText(img, f"Filtered: miu={filtered_mean:.1f}, sigma={filtered_std:.2f}", 
                   (self.margin_left + 450, stats_y), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 150, 0), 2)
        cv2.putText(img, f"Stability: +{improvement:.1f}%", 
                   (self.margin_left + 920, stats_y), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 100, 200), 2)

# ==========================
# MAIN SYSTEM
# ==========================
class WerengDetectionSystem:
    def __init__(self):
        self.config = Config()
        self.detector = EnhancedDetectionManager(self.config.MODEL_PATH, self.config)
        self.mqtt = MQTTManager(
            self.config.BROKER,
            self.config.PORT,
            self.config.USERNAME,
            self.config.PASSWORD
        )
        self.graph_renderer = EnhancedGraphRenderer(
            self.config.GRAPH_WIDTH,
            self.config.GRAPH_HEIGHT,
            self.config
        )
        
        # Data buffers
        self.x_data = deque(maxlen=self.config.BUFFER_SIZE)
        self.raw_data = deque(maxlen=self.config.BUFFER_SIZE)
        self.temporal_data = deque(maxlen=self.config.BUFFER_SIZE)
        self.filtered_data = deque(maxlen=self.config.BUFFER_SIZE)
        self.frame_index = 0
        
        # Setup windows
        cv2.namedWindow("Deteksi Wereng", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("Deteksi Wereng", self.config.WINDOW_WIDTH, self.config.WINDOW_HEIGHT)
        cv2.namedWindow("Grafik Triple Filter", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("Grafik Triple Filter", self.config.GRAPH_WIDTH, self.config.GRAPH_HEIGHT)
        
        logger.info("✅ Enhanced Detection System Ready")
        logger.info(f"   Confidence: {self.config.CONFIDENCE_THRESHOLD}")
        logger.info(f"   Image size: {self.config.IMG_SIZE}")
        logger.info(f"   Augmentation: {self.config.AUGMENT}")
    
    def get_latest_images(self):
        upload_path = Path(self.config.UPLOAD_DIR)
        if not upload_path.exists():
            return []
        
        images = []
        for ext in ['*.jpg', '*.jpeg', '*.png', '*.JPG', '*.JPEG', '*.PNG']:
            images.extend(upload_path.glob(ext))
        
        images = sorted(images, key=lambda x: x.stat().st_mtime)
        return [str(img) for img in images]
    
    def process_frame(self, image_path):
        logger.info(f"🖼️  Processing: {Path(image_path).name}")
        
        # Detect dengan preprocessing
        result = self.detector.detect(image_path)
        if result is None:
            return None
        
        boxes = result.boxes.xyxy.cpu().numpy() if result.boxes is not None else []
        confidences = result.boxes.conf.cpu().numpy() if result.boxes is not None else []
        
        raw_count = len(boxes)
        
        # Get intermediate filtered values untuk visualisasi
        temporal_filtered = self.detector.temporal.update(raw_count)
        filtered_count = self.detector.get_filtered_count(raw_count)
        
        # Store data
        self.frame_index += 1
        self.x_data.append(self.frame_index)
        self.raw_data.append(raw_count)
        self.temporal_data.append(temporal_filtered)
        self.filtered_data.append(filtered_count)
        
        avg_conf = np.mean(confidences) if len(confidences) > 0 else 0
        logger.info(f"   Raw: {raw_count} | Temporal: {temporal_filtered:.1f} | Final: {filtered_count} | Avg Conf: {avg_conf:.2f}")
        
        # Publish
        if self.mqtt.publish(self.config.TOPIC, filtered_count):
            logger.info(f"   📡 Published: {filtered_count}")
        
        # Visualize
        frame = result.plot()
        frame = self._add_info_overlay(frame, raw_count, filtered_count, avg_conf)
        frame = cv2.resize(frame, (self.config.WINDOW_WIDTH, self.config.WINDOW_HEIGHT))
        cv2.imshow("Deteksi Wereng", frame)
        
        # Graph
        graph = self.graph_renderer.render(
            self.x_data, self.raw_data, self.filtered_data, self.temporal_data
        )
        cv2.imshow("Grafik Triple Filter", graph)
        
        return filtered_count
    
    def _add_info_overlay(self, frame, raw_count, filtered_count, avg_conf):
        overlay = frame.copy()
        cv2.rectangle(overlay, (10, 10), (450, 150), (0, 0, 0), -1)
        frame = cv2.addWeighted(overlay, 0.7, frame, 0.3, 0)
        
        cv2.putText(frame, f"Raw: {raw_count}", (20, 45),
                   cv2.FONT_HERSHEY_DUPLEX, 0.9, (0, 150, 255), 2)
        cv2.putText(frame, f"Filtered: {filtered_count}", (20, 85),
                   cv2.FONT_HERSHEY_DUPLEX, 0.9, (50, 255, 50), 2)
        cv2.putText(frame, f"Confidence: {avg_conf:.2f}", (20, 125),
                   cv2.FONT_HERSHEY_DUPLEX, 0.8, (255, 200, 0), 2)
        
        return frame
    
    def run(self):
        image_index = 0
        
        while True:
            images = self.get_latest_images()
            
            if not images:
                logger.warning("⏳ Waiting for images...")
                time.sleep(2)
                continue
            
            current_image = images[image_index % len(images)]
            image_index += 1
            
            try:
                self.process_frame(current_image)
            except Exception as e:
                logger.error(f"Error: {e}", exc_info=True)
            
            key = cv2.waitKey(self.config.DISPLAY_DELAY)
            if key & 0xFF == ord('q'):
                logger.info("🛑 Exiting...")
                break
            
            time.sleep(self.config.FRAME_DELAY)
        
        cv2.destroyAllWindows()

# ==========================
# ENTRY POINT
# ==========================
if __name__ == "__main__":
    try:
        system = WerengDetectionSystem()
        system.run()
    except KeyboardInterrupt:
        logger.info("\n🛑 Stopped by user")
    except Exception as e:
        logger.error(f"❌ Fatal error: {e}", exc_info=True)
