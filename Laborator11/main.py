import os
import uuid
from flask import Flask, request, jsonify
from werkzeug.utils import secure_filename
from dotenv import load_dotenv

load_dotenv()
app = Flask(__name__)

FILES_DIR = os.path.join(os.path.dirname(__file__), "files")
os.makedirs(FILES_DIR, exist_ok=True)

def file_path(name):
    return os.path.join(FILES_DIR, secure_filename(name))

def generate_links(filename=None):
    if filename:
        return [
            {"rel": "self", "method": "GET", "href": f"/files/{filename}"},
            {"rel": "update", "method": "PUT", "href": f"/files/{filename}"},
            {"rel": "delete", "method": "DELETE", "href": f"/files/{filename}"}
        ]
    else:
        return [
            {"rel": "list", "method": "GET", "href": "/files"},
            {"rel": "create_by_name", "method": "PUT", "href": "/files/<filename>"},
            {"rel": "create_auto", "method": "POST", "href": "/files"}
        ]

@app.route("/files", methods=["GET"])
def list_files():
    files = os.listdir(FILES_DIR)
    return jsonify({
        "files": files,
        "actions": generate_links()
    }), 200

@app.route("/files/<name>", methods=["GET"])
def get_file(name):
    path = file_path(name)
    if os.path.exists(path):
        with open(path, 'r') as f:
            content = f.read()
        return jsonify({
            "filename": name,
            "content": content,
            "actions": generate_links(name)
        }), 200
    else:
        return jsonify({
            "error": "File not found",
            "actions": generate_links()
        }), 404

@app.route("/files/<name>", methods=["PUT"])
def put_file(name):
    content = request.get_data(as_text=True)
    path = file_path(name)
    created = not os.path.exists(path)
    with open(path, 'w') as f:
        f.write(content)
    return jsonify({
        "message": "Created" if created else "Updated",
        "filename": name,
        "actions": generate_links(name)
    }), 201 if created else 204

@app.route("/files", methods=["POST"])
def create_file_auto():
    content = request.get_data(as_text=True)
    filename = f"{uuid.uuid4().hex}.txt"
    path = file_path(filename)
    with open(path, 'w') as f:
        f.write(content)
    return jsonify({
        "message": "File created",
        "filename": filename,
        "actions": generate_links(filename)
    }), 201

@app.route("/files/<name>", methods=["DELETE"])
def delete_file(name):
    path = file_path(name)
    if os.path.exists(path):
        os.remove(path)
        return jsonify({
            "message": f"{name} deleted",
            "actions": generate_links()
        }), 200
    else:
        return jsonify({
            "error": "File not found",
            "actions": generate_links()
        }), 404

if __name__ == "__main__":
    app.run(debug=True)
