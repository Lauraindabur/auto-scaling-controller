import logging
import os

from flask import Flask

app = Flask(__name__)
logging.getLogger("werkzeug").setLevel(logging.WARNING)

# Puerto configurable: en la AMI se deja en 80 (valor por defecto, requiere
# root); en local se puede correr sin sudo con PORT=8080 (ver README).
PORT = int(os.environ.get("PORT", 80))


@app.route("/")
def carga():
    total = 0
    for i in range(2_000_000):
        total += i
    return f"ok {total}"


@app.route("/health")
def health():
    return "ok", 200


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=PORT, threaded=True)
