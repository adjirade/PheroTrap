#!/usr/bin/env python3
import time
import ssl
import random
import logging
import paho.mqtt.client as mqtt

BROKER = "da3a4cf65279448b9e7b5f08fa2f0d3c.s1.eu.hivemq.cloud"
PORT = 8883
TOPIC = "CAM1"
USERNAME = "Phero1"
PASSWORD = "Budakonyot1"
INTERVAL = 10  # detik

logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(message)s")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logging.info("✅ Terhubung ke broker!")
    else:
        logging.error(f"❌ Gagal konek, kode rc = {rc}")

def main():
    client = mqtt.Client()
    client.username_pw_set(USERNAME, PASSWORD)

    context = ssl.create_default_context()
    client.tls_set_context(context)
    client.on_connect = on_connect

    client.connect(BROKER, PORT, keepalive=60)
    client.loop_start()

    try:
        while True:
            jumlah = random.randint(0, 99)
            payload = f"JUMLAH WERENG = {jumlah}"
            client.publish(TOPIC, payload, qos=0)
            logging.info(f"Dikirim: {payload}")
            time.sleep(INTERVAL)
    except KeyboardInterrupt:
        logging.info("🛑 Dihentikan oleh user.")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    main()
