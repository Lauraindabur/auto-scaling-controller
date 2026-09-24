from flask import Flask

app = Flask(__name__)


# Endpoint de carga: el mismo bucle de CPU que ya usaba infra/user-data.sh.
# Sirve para que scripts/load-test.sh genere carga real de CPU/latencia.
@app.route("/")
def carga():
    total = 0
    for i in range(2_000_000):
        total += i
    return f"ok {total}"


# Endpoint de salud: responde de inmediato, sin el bucle. Lo usa el Target
# Group para el Health Check, para no depender de cuánto tarde el endpoint
# de carga bajo tráfico alto.
@app.route("/health")
def health():
    return "ok"


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=80)
