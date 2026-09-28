# Pruebas de carga con k6

Este documento describe como generar la carga de prueba del experimento completo contra el controlador desplegado en AWS.

## 1. Prerrequisitos

- `k6` instalado localmente (o en la maquina desde la que se lanza la carga).
- El ALB con al menos una instancia healthy en el Target Group (ver [deployment.md](./deployment.md)).


## 2. El comando real

```bash
scripts/k6-escenario.sh --dns <dns-del-alb>
```

Por ejemplo:

```bash
scripts/k6-escenario.sh --dns controller-lb-123456789.us-east-1.elb.amazonaws.com
```

Este comando:

1. Hace un preflight (5x `/health` + 1x `/`) contra el DNS dado; si no responde 200, no genera carga.
2. Muestra el plan del experimento (fases, duracion total, VUs, timeout) y pide confirmar escribiendo `si`.
3. Corre `loadtest/k6/escenario_completo.js`, que lee el perfil de fases de `loadtest/scenarios/escenario_completo.csv` (o el que se pase con `--scenario`).
4. Guarda los resultados en `results/k6-escenario/<fecha UTC>/`: `raw.csv`, `summary.json`, `escenario.csv` (copia del perfil usado), `run_info.txt` (con `inicio_utc`/`fin_utc`) y `run.log`.

`Ctrl+C` detiene la corrida de inmediato.

### Opciones

| Opcion | Variable de entorno | Default | Descripcion |
|---|---|---|---|
| `--dns <host>` | `LB_DNS` | (obligatoria) | DNS del ALB, o `localhost` |
| `--scenario <archivo>` | `SCENARIO_CSV` | `loadtest/scenarios/escenario_completo.csv` | Perfil de fases (csv) |
| `--c-rpm <n>` | `C_RPM` | `480` | Capacidad de 1 instancia, en RPM. Debe coincidir con `C_RPM` del controller |
| `--pre-vus <n>` | `PRE_ALLOCATED_VUS` | `50` | VUs preasignadas |
| `--max-vus <n>` | `MAX_VUS` | `400` | VUs maximas |
| `--req-timeout <dur>` | `REQ_TIMEOUT` | `10s` | Timeout de cada peticion (formato k6) |
| `--allow-ip` | `ALLOW_IP` | `0` | Permite apuntar a una IP en vez de un DNS. No pasa por el Target Group: no sirve para probar el autoescalado, solo para calibrar una instancia suelta |
| `--yes` / `-y` | - | - | No pedir confirmacion |

Las tasas del perfil estan expresadas en **multiplos de C** (la capacidad de una instancia), no en RPM fijos, para que el mismo CSV siga siendo valido si se recalibra `C_RPM`. Por eso `--c-rpm` debe coincidir con el `C_RPM` configurado en el controller (`config/controller.env`): si no coinciden, "meseta a 3xC" deja de significar lo que dice el perfil.

## 3. Que hace cada fase de `escenario_completo.csv`

```csv
fase,multiplo_inicio,multiplo_fin,duracion_seg,salto_intencional
base,0.4,0.4,300,0
pico,2.0,2.0,60,1
recuperacion,0.4,0.4,240,1
rampa,0.4,3.0,720,0
meseta,3.0,3.0,600,0
bajada,3.0,0.4,480,0
reposo,0.0,0.0,800,1
```

Si `multiplo_inicio == multiplo_fin` la fase es plana (tasa constante). Si son distintos, k6 interpola la tasa linealmente a lo largo de la fase. `salto_intencional=1` marca una fase donde la tasa cambia de golpe respecto a la fase anterior (a proposito, para ejercitar la reaccion del controller ante un escalon), en vez de una transicion suave.

| Fase | Multiplo de C | Duracion | Proposito |
|---|---|---|---|
| `base` | 0.4x constante | 300 s (5 min) | Trafico bajo estable, punto de partida. El controller deberia mantenerse en el minimo. |
| `pico` | 2.0x constante (salto) | 60 s (1 min) | Pico corto y subito de trafico, para ejercitar la guarda anti-oscilacion y ver si el controller reacciona sin sobre-corregir por un pico breve. |
| `recuperacion` | 0.4x constante (salto) | 240 s (4 min) | Vuelve de golpe al trafico base tras el pico, para observar si el controller no queda "pegado" en capacidad alta. |
| `rampa` | 0.4x -> 3.0x lineal | 720 s (12 min) | Subida gradual y sostenida de trafico, el caso principal para medir el tiempo de reaccion del proactivo (Holt) y del reactivo (CPU) frente a demanda creciente. |
| `meseta` | 3.0x constante | 600 s (10 min) | Saturacion sostenida en el techo de la rampa, para verificar que el controller llega y se mantiene en la capacidad necesaria (S = D) el tiempo suficiente. |
| `bajada` | 3.0x -> 0.4x lineal | 480 s (8 min) | Descenso gradual de trafico, para medir el tiempo de descenso (desde que D baja hasta que S = D) bajo el cooldown de bajada. |
| `reposo` | 0.0x constante (salto) | 800 s (13 min 20 s) | Sin trafico. Da tiempo a que el controller termine de bajar de 5 a 1 instancia (hasta 4 recortes x 240 s de cooldown = 960 s, mas la ventana de la MA) antes de que termine el experimento. |

Duracion total: 300 + 60 + 240 + 720 + 600 + 480 + 800 = **3200 s (~53 min 20 s)**.


## 4. Siguiente paso

El propio script indica el comando siguiente:

```bash
scripts/build_timeline.py --run-dir <resultado> --decisions logs/decisions.jsonl --p90-csv <export de CloudWatch>
```

`build_timeline.py` cruza `run_info.txt` (inicio/fin de cada fase por reloj de pared) con `logs/decisions.jsonl` (lo que el controller decidio en vivo) para generar `timeline.csv` y el resumen de evaluacion; `plot_timeline.py` genera la grafica de 4 paneles a partir de ese `timeline.csv`.


Los perfiles disponibles en `loadtest/scenarios/debug/` son `test_pico.csv`, `test_rampa.csv`, `test_salto.csv`, `test_bajada.csv` y `test_probar_reactivo.csv`, cada uno con el mismo formato de columnas (`fase,multiplo_inicio,multiplo_fin,duracion_seg,salto_intencional`).
