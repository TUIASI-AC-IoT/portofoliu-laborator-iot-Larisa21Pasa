from flask import Flask, request, jsonify
from flask_jwt_extended import JWTManager, create_access_token, jwt_required, get_jwt_identity
import os, json

app = Flask(__name__)
app.config["JWT_SECRET_KEY"] = "super-secret-jwt-key"
jwt = JWTManager(app)

# hardcoded users for demo purposes
users = {
    "user1": {"password": "parola1", "role": "admin"},
    "user2": {"password": "parola2", "role": "owner"},
    "user3": {"password": "parolaX", "role": "owner"},
}

jwt_store = {}
DATA_FILE = "sensor_1.json"

@app.route("/auth", methods=["POST"])
def login():
    creds = request.get_json()
    username = creds.get("username")
    password = creds.get("password")
    user = users.get(username)
    if not user or user["password"] != password:
        return jsonify({"error": "Invalid credentials"}), 401
    access_token = create_access_token(identity=username)
    jwt_store[access_token] = user["role"]
    return jsonify({"access_token": access_token}), 200

@app.route("/auth/jwtStore", methods=["GET"])
@jwt_required()
def validate_token():
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    role = jwt_store.get(token)
    if not role:
        return jsonify({"error": "Token not found"}), 404
    return jsonify({"role": role}), 200

@app.route("/auth/jwtStore", methods=["DELETE"])
@jwt_required()
def logout():
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    if token in jwt_store:
        del jwt_store[token]
        return jsonify({"message": "Logged out"}), 200
    return jsonify({"error": "Token not found"}), 404

@app.route("/sensor/1", methods=["GET"])
@jwt_required()
def get_sensor():
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    role = jwt_store.get(token)
    if role not in ["admin", "owner"]:
        return jsonify({"error": "Access denied"}), 403
    if not os.path.exists(DATA_FILE):
        return jsonify({"error": "not found"}), 404
    with open(DATA_FILE) as f:
        data = json.load(f)
    return jsonify(data)

@app.route("/sensor/1", methods=["POST"])
@jwt_required()
def create_sensor():
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    role = jwt_store.get(token)
    if role != "admin":
        return jsonify({"error": "Access denied"}), 403
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
@jwt_required()
def update_target():
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    role = jwt_store.get(token)
    if role != "admin":
        return jsonify({"error": "Access denied"}), 403
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
