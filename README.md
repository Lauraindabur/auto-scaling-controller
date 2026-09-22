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

## 3. Configuración del controller

Toda la configuración viene de variables de entorno, todas **obligatorias** y sin valores por
defecto; el archivo de ejemplo es `config/controller.env` (ignorado por git porque contiene
ARN de la cuenta).

| Variable | Valor | Significado |
|---|---|---|
| `ASG_NAME` | `controller-asg` | Auto Scaling Group a gobernar |
| `AWS_REGION` | `us-east-1` | Región |
| `TARGET_GROUP_ARN` | `arn:aws:elasticloadbalancing:...:targetgroup/controller-target-group/<id>` | Target Group (salud y métrica de RT) |
| `LOAD_BALANCER_ARN` | `arn:aws:elasticloadbalancing:...:loadbalancer/app/controller-lb/<id>` | ALB (métrica de RT) |
| `UMBRAL_SUBIDA` / `UMBRAL_BAJADA` | `70.0` / `30.0` | Umbrales de CPU (%) |
| `UMBRAL_RT_SUBIDA` / `UMBRAL_RT_BAJADA` | `1.0` / `1.0` | Umbrales de tiempo de respuesta (s) |
| `VENTANA_MA` | `3` | Muestras de cada media móvil |
| `COOLDOWN_CICLOS` | `3` | Ciclos de espera tras confirmar (o fallar por timeout) una operación |
| `TIMEOUT_OPERACION_CICLOS` | `6` | Ciclos máximos que una operación puede estar `IN_PROGRESS` |
| `CAPACIDAD_MIN` / `CAPACIDAD_MAX` | `1` / `5` | Límites de capacidad |
| `PASO_MAXIMO_SUBIDA` | `2` | Máximo de instancias por decisión de subida |
| `INTERVALO_CICLO_SEGUNDOS` | `60` | Segundos de espera entre ciclos |

**Validación al iniciar.** Si una variable falta, está vacía, no es un número, o los valores
son incoherentes, el controller termina con código 1 y un mensaje claro, antes de tocar AWS:

- `VENTANA_MA >= 1`, `TIMEOUT_OPERACION_CICLOS >= 1`, `PASO_MAXIMO_SUBIDA >= 1`,
  `INTERVALO_CICLO_SEGUNDOS >= 1`, `COOLDOWN_CICLOS >= 0`.
- `0 <= UMBRAL_BAJADA < UMBRAL_SUBIDA <= 100`.
- `0 < UMBRAL_RT_BAJADA <= UMBRAL_RT_SUBIDA`.
- `1 <= CAPACIDAD_MIN <= CAPACIDAD_MAX`.
- Los ARN del ALB y del Target Group deben poder interpretarse (contener `app/` y
  `targetgroup/`), porque el RT es métrica de decisión y sin ellos el controller fallaría
  todos los ciclos.

Ejemplo: `UMBRAL_SUBIDA=abc` → `Error al iniciar el controller: UMBRAL_SUBIDA debe ser un número, valor recibido: 'abc'`.

---

## 4. Despliegue y ejecución

### Requisitos

- Linux (probado en Ubuntu sobre WSL2), CMake ≥ 3.13, g++ con C++17.
- AWS SDK for C++ instalado con vcpkg: `aws-sdk-cpp[monitoring,autoscaling,elasticloadbalancingv2]`.
- Credenciales de AWS con los permisos de la sección 2 (`aws configure` o variables `AWS_*`).

### Compilar

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

(Ajusta la ruta del toolchain si vcpkg está en otro lugar, por ejemplo `~/vcpkg`.)

### Ejecutar

Desde la raíz del proyecto, porque el log se escribe en la ruta relativa `logs/decisions.jsonl`
(la carpeta `logs/` debe existir):

```bash
mkdir -p logs
source config/controller.env
./build/controller
```

- Corre en primer plano hasta `Ctrl+C`. Escribe una línea de log por ciclo y no imprime nada en pantalla.
- **Cambia la capacidad real del ASG** `ASG_NAME`: comprueba nombres, región y ARN antes de arrancar.
- Al arrancar prellena las medias móviles con ~3 minutos de historial; hasta tener 3 muestras
  de cada métrica registra "historial insuficiente".
- El controller es un proceso simple sin persistencia: si se detiene, no escala; si se reinicia,
  pierde el estado de cooldown y de la operación en curso.

### Seguir lo que hace

```bash
tail -f logs/decisions.jsonl | jq -c '[.timestamp,.moving_average_cpu,.moving_average_response_time,.current_capacity,.decision,.justification]'
```

---

## 5. Generación de carga

[scripts/load-test.sh](scripts/load-test.sh) usa **Apache Bench (`ab`)** contra el **DNS del
Load Balancer** (nunca la IP de una instancia, para que el ALB reparta el tráfico y las
instancias nuevas reciban carga). Se ejecuta desde WSL/Ubuntu.

```bash
sudo apt update && sudo apt install -y apache2-utils     # una sola vez
scripts/load-test.sh check     # 10 requests, 1 conexión: verifica el ALB y mide ms/request
scripts/load-test.sh           # prueba completa (pide escribir "si")
scripts/load-test.sh idle      # solo la fase de reposo, sin generar tráfico
```

Opciones (o las variables de entorno indicadas): `--dns` (`LB_DNS`), `--levels "2 8 16"`
(`LEVELS`), `--duration <s>` (`LEVEL_DURATION_SECONDS`), `--idle-minutes <min>`
(`IDLE_MINUTES`), `--yes`. El DNS por defecto está en el script; si recreas el ALB, pásalo
con `--dns <dns-del-alb>`.

### Procedimiento

1. **Niveles crecientes**, en secuencia y sin pausa: concurrencia **2 → 8 → 16**, 600 s cada uno.
2. **Fase de reposo** de 20 minutos sin enviar tráfico ("bajar la carga"): el script anuncia
   `GENERADOR DETENIDO` y así se observa el escalado hacia abajo.
3. Cada evento imprime una **marca de tiempo UTC** al inicio y al final de cada nivel, para correlacionar con `logs/decisions.jsonl`.


---

## 6. Registro de decisiones

El controller escribe **una línea JSON por ciclo** en `logs/decisions.jsonl` (formato JSONL,
modo *append*, se vacía a disco en cada línea). Registra todos los ciclos, también los de
mantenimiento, fallo de métrica, cooldown u operación en curso. `logs/` está ignorado por git.

### Ejemplo (subida por tiempo de respuesta)

```json
{"timestamp":"2026-09-21T18:27:01Z","cycle_id":1,"cpu_utilization":45.200000,"moving_average_cpu":41.700000,"target_response_time":2.400000,"moving_average_response_time":2.500000,"current_capacity":1,"decision":"INCREASE_CAPACITY","decision_trigger":"RT","justification":"MA_RT por encima del umbral de subida; capacidad objetivo 3, paso +2","requested_action":"INCREASE_CAPACITY -> 3","action_result":"IN_PROGRESS","operation_state":"IN_PROGRESS","cooldown_remaining":0,"request_count_per_target":310.000000}
```

### Campos

| Campo | Significado |
|---|---|
| `timestamp` | Hora UTC (`YYYY-MM-DDTHH:MM:SSZ`), la misma referencia que usa `load-test.sh` |
| `cycle_id` | Contador de ciclos desde que arrancó el proceso |
| `cpu_utilization`, `target_response_time` | Muestras crudas de ese ciclo (%, segundos); `null` si el ciclo falló |
| `moving_average_cpu`, `moving_average_response_time` | Medias móviles; `null` hasta tener historial suficiente |
| `current_capacity` | Capacidad deseada del ASG al evaluar; `null` si el ciclo terminó antes de consultarla |
| `decision` | `INCREASE_CAPACITY`, `REDUCE_CAPACITY` o `MAINTAIN_CAPACITY` |
| `decision_trigger` | Qué métrica motivó la decisión: `CPU`, `RT`, `CPU+RT` o `NONE` |
| `justification` | Motivo en texto (ver abajo) |
| `requested_action` | Acción y capacidad solicitada al ASG (`INCREASE_CAPACITY -> 3`) o `NONE` |
| `action_result` | `N/A`, `IN_PROGRESS` (orden aceptada), `SUCCESSFUL` (operación confirmada), `FAILED` (error de API o timeout) |
| `operation_state` | Estado de la operación: `NONE`, `IN_PROGRESS` o `FAILED` (solo en el ciclo en que se declara el timeout) |
| `cooldown_remaining` | Ciclos de cooldown que quedan |
| `request_count_per_target` | `RequestCountPerTarget` del ALB; solo observabilidad, **no** interviene en la decisión |

### Justificaciones que puede registrar

- `historial insuficiente: MA_CPU` / `MA_RT` / `MA_CPU y MA_RT`
- `CPUUtilization no disponible tras agotar reintentos` (o `TargetResponseTime`, o ambas)
- `operación en curso`
- `operación confirmada, inicia cooldown`
- `operación excedió el tiempo máximo (6 ciclos), se marca como fallida`
- `en periodo de cooldown`
- Subida: `MA_CPU por encima del umbral de subida; capacidad objetivo 2, paso +1`,
  `MA_RT por encima del umbral de subida; capacidad objetivo 3, paso +2`,
  `MA_CPU y MA_RT por encima de sus umbrales de subida; capacidad objetivo ..., paso +...`
- Bajada: `MA_CPU y MA_RT por debajo de sus umbrales de bajada`
- `límite máximo alcanzado (...)`, `límite mínimo alcanzado (...)`, `no seguro reducir`
- `MA_CPU baja pero MA_RT no está por debajo del umbral de bajada`
- `dentro del rango esperado`
- `capacidad actual no disponible`


---

## 8. Estructura del repositorio

```
include/, src/        Código del controller (DecisionEngine, MetricSource, ASGActuator, Logger, ...)
tests/                Pruebas offline y su CMakeLists.txt independiente
infra/user-data.sh    User-data del Launch Template (app Flask en :80)
scripts/load-test.sh  Generador de carga escalonada con Apache Bench
config/               controller.env (variables de entorno)
logs/                 decisions.jsonl (generado al ejecutar)
results/              Resultados de las pruebas de carga (generado)
CMakeLists.txt        Build principal (usa el AWS SDK vía vcpkg)
```

---
-->