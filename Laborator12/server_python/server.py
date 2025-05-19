from flask import Flask, request, jsonify
import os, json

app = Flask(__name__)
DATA_FILE = "sensor_1.json"

@app.route("/sensor/1", methods=["GET"])
def get_sensor():
    if not os.path.exists(DATA_FILE):
        return jsonify({"error": "not found"}), 404
    with open(DATA_FILE) as f:
        data = json.load(f)
    return jsonify(data)

@app.route("/sensor/1", methods=["POST"])
def create_sensor():
    if os.path.exists(DATA_FILE):
        return jsonify({"error": "already exists"}), 409
    data = {
        "id": "sensor1",
        "value": 21.0,
        "target": 23.0
    }
    with open(DATA_FILE, "w") as f:
        json.dump(data, f)
    return jsonify({"message": "created"}), 201

@app.route("/sensor/1", methods=["PUT"])
def update_target():
    if not os.path.exists(DATA_FILE):
        return jsonify({"error": "not found"}), 404
    req = request.get_json()
    if "target" not in req:
        return jsonify({"error": "missing target"}), 400
    with open(DATA_FILE) as f:
        data = json.load(f)
    data["target"] = req["target"]
    with open(DATA_FILE, "w") as f:
        json.dump(data, f)
    return jsonify({"message": "target updated"}), 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
