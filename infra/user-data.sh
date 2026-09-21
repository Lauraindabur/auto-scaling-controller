#!/bin/bash
# User-data del Launch Template (Ubuntu 24.04 LTS): deja una app Flask en :80
# para que la instancia se registre en el Target Group y pase los Health Checks.

# Instala pip y Flask desde los repositorios de Ubuntu.
apt update
apt install -y python3-pip python3-flask

# Crea la app: cada request a "/" hace un bucle de 2 millones de sumas para
# generar carga de CPU (sirve para disparar el auto scaling).
cat > /home/ubuntu/app.py << 'EOF'
from flask import Flask

app = Flask(__name__)

@app.route("/")
def carga():
    total = 0
    for i in range(2_000_000):
        total += i
    return f"ok {total}"

app.run(host="0.0.0.0", port=80)
EOF

# Arranca la app en segundo plano; la salida se guarda en app.log.
nohup python3 /home/ubuntu/app.py \
    > /home/ubuntu/app.log 2>&1 &
