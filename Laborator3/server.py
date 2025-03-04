import io
from flask import Flask, send_file
import os.path

app = Flask(__name__)

@app.route('/firmware.bin')
def firm():
    with open(".pio\\build\\esp-wrover-kit\\firmware.bin", 'rb') as bites:
        print(bites)
        return send_file(
                     io.BytesIO(bites.read()),
                     mimetype='application/octet-stream'
               )

@app.route("/version")
def version():
     with open("versioning", 'rb') as bites:
        print("HELLOOOOOOOOOOOOOOOOOO")
        print(bites)
        return send_file(
                     io.BytesIO(bites.read()),
                     mimetype='application/octet-stream'
               )
     
@app.route("/versioning")
def versioning():
     with open("versioning") as f:
        print("HELLOOOOOOOOOOOOOOOOOO")
        v = f.readline()
        print("veriunsea ", v)
        return v
                   
@app.route("/")
def hello():
    return("index file")

if __name__ == '__main__':
    app.run(host='0.0.0.0', ssl_context=('ca_cert.pem', 'ca_key.pem'), debug=True)