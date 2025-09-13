import paho.mqtt.client as mqtt
import sqlite3

BROKER = "localhost"       # Broker is running locally on RPi
PORT = 1883                # Default MQTT port
TOPIC = "test"    # Your topic to subscribe to

# DB
DB_PATH = "door_data.db"  # Path to your SQLite database file

# Callback when the client connects to the broker
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT Broker!")
        client.subscribe(TOPIC)
        print(f"Subscribed to topic: {TOPIC}")
    else:
        print(f"Failed to connect. Return code {rc}")

# Callback when a message is received from the broker
def on_message(client, userdata, msg):
    print(f"Received on {msg.topic}: {msg.payload.decode()}")
    
    # Connect to database (creates file if not exists)
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    # Insert new row (timestamp is filled automatically)
    cursor.execute("INSERT INTO door_status (status) VALUES (?)", (msg.payload.decode(),))

    conn.commit()
    conn.close()


# Create MQTT client and attach callbacks
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

print(f"Connecting to broker at {BROKER}:{PORT}...")
client.connect(BROKER, PORT, keepalive=60)

# Loop forever, waiting for messages
client.loop_forever()
