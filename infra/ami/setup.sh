#!/bin/bash
# =============================================================================
# setup.sh - Pasos para preparar la instancia temporal (i-AppBuilder) antes de
# crear la AMI. Se ejecuta A MANO por SSH dentro de la instancia (no es
# user-data): copia este archivo, app.py y flask-app.service a la instancia y
# corre este script con sudo.
#
# Desde tu máquina (WSL), con la instancia ya lanzada y su IP pública:
#   scp -i vockey.pem infra/ami/app.py infra/ami/flask-app.service infra/ami/setup.sh \
#       ubuntu@<ip-publica>:/home/ubuntu/
#   ssh -i vockey.pem ubuntu@<ip-publica>
#   sudo bash setup.sh
# =============================================================================
set -euo pipefail

echo "Instalando Python 3 y Flask..."
apt update
apt install -y python3-flask

echo "Instalando el servicio systemd..."
cp /home/ubuntu/flask-app.service /etc/systemd/system/flask-app.service
# app.py ya debe estar en /home/ubuntu/app.py (copiado por scp junto con este script).
touch /home/ubuntu/app.log
chown ubuntu:ubuntu /home/ubuntu/app.py /home/ubuntu/app.log

echo "Habilitando y arrancando el servicio..."
systemctl daemon-reload
systemctl enable flask-app
systemctl restart flask-app

echo "Estado del servicio:"
systemctl --no-pager status flask-app

echo
echo "Prueba local:"
sleep 1
curl -s localhost/health && echo
curl -s localhost/ && echo

echo
echo "Listo. Verifica también desde fuera con:"
echo "  curl http://<ip-publica>/health"
echo "  curl http://<ip-publica>/"
echo
echo "Cuando confirmes que responde, sigue con la Fase 1 (Actions > Image > Create image)."
