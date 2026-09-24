# auto-scaling-controller

Controller de autoescalado en C++ para AWS. Cada 60 segundos lee desde CloudWatch `CPUUtilization` de las instancias y `TargetResponseTime` del Application
Load Balancer, las suaviza con una media móvil de 3 muestras y decide si subir, bajar o
mantener la capacidad de un Auto Scaling Group. Cada decisión queda registrada en un log
JSON con su justificación.


---

## 1. Cómo decide

### Política

`MA_CPU` y `MA_RT` son las medias móviles (3 muestras) de `CPUUtilization` (%) y de
`TargetResponseTime` (segundos).

| Decisión | Condición | Paso |
|---|---|---|
| **Subir** | `MA_CPU > 70` **o** `MA_RT > 1.0` | proporcional al exceso (ver abajo) |
| **Bajar** | `MA_CPU < 30` **y** `MA_RT < 1.0`, con todas las instancias restantes Healthy | −1 |
| **Mantener** | cualquier otro caso | — |

La asimetría es deliberada: para subir basta con que **una** métrica detecte el problema
(protege el servicio); para bajar hacen falta **las dos** (retirar capacidad es conservador).
Los umbrales son estrictos: con `MA_RT` exactamente en 1.0 no se sube ni se baja.

Capacidad mínima 1 y máxima 5. Si ya está en el máximo, la decisión es `MAINTAIN_CAPACITY`
con la justificación "límite máximo alcanzado" (y análogo en el mínimo).

<!--
### Paso de subida proporcional

Se basa en un modelo medido: el tiempo de respuesta se reparte entre las instancias,
`tiempo_respuesta ≈ (concurrencia / instancias) × tiempo_por_petición`. Despejando:

```
N_necesarias = techo( N_actual × MA_RT / UMBRAL_RT_SUBIDA )
N_necesarias = min(N_necesarias, CAPACIDAD_MAX)
paso         = min(N_necesarias − N_actual, PASO_MAXIMO_SUBIDA)     (nunca menos de +1)
```

- Se aplica solo si `MA_RT` supera su umbral (disparador `RT` o `CPU+RT`).
- Si la subida la disparó **solo la CPU**, el paso es +1: no hay un modelo medido que
  relacione CPU con capacidad.
- Ejemplos con `PASO_MAXIMO_SUBIDA=2`: capacidad 1 y `MA_RT`=1.319 → +1; capacidad 1 y
  `MA_RT`=2.5 → +2; capacidad 1 y `MA_RT`=4.0 → pediría +3, queda en +2.

### Ciclo (cada `INTERVALO_CICLO_SEGUNDOS`)

1. **Métricas.** Consulta CPU y RT juntas. Si falla cualquiera de las dos, el ciclo entero es
   un fallo: `MAINTAIN_CAPACITY` y no se actualiza ninguna media móvil.
   - Para RT, "CloudWatch respondió sin datapoints" (no hubo tráfico) **no** es un fallo: la
     muestra vale 0.0. Solo un error real de la API cuenta como fallo.
2. **Historial.** Agrega las muestras. Si alguna de las dos medias tiene menos de 3 muestras:
   `MAINTAIN_CAPACITY` ("historial insuficiente"). Al arrancar se prellenan con los últimos
   ~3 minutos de CloudWatch.
3. **Operación en curso.** Si el estado es `IN_PROGRESS`:
   - si la operación se confirmó (instancias Healthy en el Target Group = capacidad deseada)
     → se marca exitosa y **arranca el cooldown**;
   - si lleva más de `TIMEOUT_OPERACION_CICLOS` ciclos sin confirmarse → se marca fallida, el
     estado vuelve a `NONE` y **arranca el cooldown**. No se deshace nada;
   - si no → `MAINTAIN_CAPACITY` ("operación en curso").
4. **Cooldown.** Si está activo, `MAINTAIN_CAPACITY` y descuenta un ciclo. Dura
   `COOLDOWN_CICLOS` ciclos y empieza cuando la operación se confirma (no cuando se dispara).
5. **Evaluación.** Aplica la política de arriba y, si corresponde, llama a
   `SetDesiredCapacity`. Si esa llamada falla, la capacidad no cambia y el estado vuelve a
   `NONE` de inmediato, **sin** cooldown (un fallo de API deja el sistema en un estado conocido).
6. **Registro.** Escribe una línea en `logs/decisions.jsonl` (sección 6).

### Componentes

| Componente | Responsabilidad |
|---|---|
| `DecisionEngine` | El ciclo de arriba: aplica la política y coordina todo lo demás |
| `MovingAverage` | Media móvil de N muestras (una instancia para CPU y otra para RT) |
| `CooldownManager` | Estado de la operación (`NONE`/`IN_PROGRESS`) y ciclos de cooldown |
| `IMetricSource` / `MetricSource` | Lee CloudWatch (CPU, RT y `RequestCountPerTarget`) |
| `IActuator` / `ASGActuator` | Lee y cambia la capacidad del ASG; consulta la salud en el Target Group |
| `Logger` | Escribe el log JSON de decisiones |
| `Config` | Lee y valida las variables de entorno |

`IMetricSource` e `IActuator` son interfaces mínimas sin dependencias del AWS SDK: permiten
probar `DecisionEngine` sin AWS usando fakes.

---

## 2. Construcción de la AMI

La app de las instancias del ASG ([infra/app/app.py](infra/app/app.py) +
[infra/app/app.service](infra/app/app.service)) ya no se instala con user-data en cada
arranque: se hornea una vez en una **AMI propia**, así las instancias nuevas arrancan con
la app lista y no dependen de que `apt install` funcione en ese momento. Esto reemplaza a
[infra/user-data.sh](infra/user-data.sh) para las instancias del ASG: el Launch Template
que use esta AMI va **sin user-data**, y el health check del Target Group debe apuntar a
`/health` (no a `/`, que es el endpoint de carga).

### Procedimiento (manual, sobre una instancia temporal)

Instancia temporal: Ubuntu Server 24.04 LTS, `t2.micro`, en una subred pública (solo para
la construcción; se termina después de crear la AMI).

1. `sudo apt update && sudo apt install -y python3-flask`
2. Copiar `app.py` a `/opt/app/app.py` y `app.service` a
   `/etc/systemd/system/app.service`.
3. `sudo systemctl daemon-reload && sudo systemctl enable --now app`
4. Verificar con `curl http://localhost/health` y `curl http://localhost/`.
5. Prueba de reinicio: `sudo reboot` y, sin volver a entrar por SSH, comprobar
   `curl http://<ip-publica-temporal>/health` desde fuera — confirma que
   `Restart=always` y `enable` dejan la app funcionando sola tras un arranque en frío.
6. `sudo apt clean` y crear la imagen (**Actions → Image → Create image**) con nombre
   `ami-autoscaling-app-v1`.

### Cómo correr la app en local (para probarla antes de hornear la AMI)

```bash
cd infra/app
python3 -m venv .venv && source .venv/bin/activate   # opcional, recomendado
pip install flask
PORT=8080 python3 app.py
```

En otra terminal: `curl http://localhost:8080/` y `curl http://localhost:8080/health`.
Sin `sudo`: el puerto 8080 no es privilegiado, a diferencia del 80 (que exige root, por
eso `app.service` corre como `User=root`). `infra/app/.venv/` está en `.gitignore`.

---

