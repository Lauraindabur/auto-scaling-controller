#!/usr/bin/env python3
"""Arma timeline.csv (Paso 9) uniendo logs/decisions.jsonl + el perfil de fases del
experimento + (opcional) un export de CloudWatch con la latencia p90, y calcula el
resumen de evaluacion (minutos sub/sobre-aprovisionado, tiempo de reaccion, tiempo de
descenso, adaptaciones, instancia-minutos, % de minutos con SLO cumplido).

Solo usa la biblioteca estandar de Python (sin dependencias externas).

Cada linea de decisions.jsonl ya es un minuto (PERIOD_SEG=60 en todo el diseño), asi
que no hace falta re-agrupar: una linea = una fila de timeline.csv.

D (necesarias) se recalcula aqui a partir de la demanda REAL observada
(demanda_rpm = request_count), no del pronostico: es la metrica de evaluacion "a toro
pasado" de la seccion 8, distinta de needed_instances (que el controller calculo en vivo
con el pronostico de Holt y sale tal cual del log).

Para el export de CloudWatch (p90 de TargetResponseTime, en SEGUNDOS en CloudWatch):

    aws cloudwatch get-metric-data \\
      --start-time <inicio_utc> --end-time <fin_utc> --region <region> \\
      --metric-data-queries '[{
        "Id": "p90",
        "MetricStat": {
          "Metric": {
            "Namespace": "AWS/ApplicationELB",
            "MetricName": "TargetResponseTime",
            "Dimensions": [
              {"Name": "LoadBalancer", "Value": "app/ALB-App/<id>"},
              {"Name": "TargetGroup", "Value": "targetgroup/TG-App/<id>"}
            ]
          },
          "Period": 60,
          "Stat": "p90"
        },
        "ReturnData": true
      }]' \\
      --query 'MetricDataResults[0]' --output json > p90.json

Recomendado ademas (diagnostico, no entra en timeline.csv ni en el resumen): exportar
CPUCreditBalance de las instancias t2.micro durante la fase "meseta" (600s a ~3xC), con el
mismo comando cambiando Namespace a "AWS/EC2", MetricName a "CPUCreditBalance", Stat a
"Average" y Dimensions a [{"Name":"AutoScalingGroupName","Value":"ASG-App"}]. Si el saldo
de creditos cae a 0 durante la meseta, el throttling hace que CPUUtilization deje de
reflejar la carga real justo en el tramo mas interesante del experimento (riesgo anotado
desde el Paso 0). Revisar ese export aparte, a mano, al interpretar los resultados.

Uso:
    python3 scripts/build_timeline.py \\
        --decisions logs/decisions.jsonl \\
        --scenario loadtest/scenarios/escenario_completo.csv \\
        --run-info results/k6-escenario/<run_id>/run_info.txt \\
        [--p90-json p90.json] \\
        [--out timeline.csv] [--c-rpm 480] [--min-capacity 1] [--max-capacity 5] \\
        [--slo-ms 500]
"""
import argparse
import csv
import json
import sys
from datetime import datetime, timezone


# --- Lectura de las 3 fuentes ------------------------------------------------------

def parse_iso(texto):
    """"2026-09-24T12:00:00Z" -> datetime UTC. Tambien acepta con milisegundos."""
    texto = texto.strip()
    if texto.endswith("Z"):
        texto = texto[:-1] + "+00:00"
    return datetime.fromisoformat(texto).astimezone(timezone.utc)


def epoch(dt):
    return int(dt.timestamp())


def leer_decisiones(ruta):
    filas = []
    with open(ruta, encoding="utf-8") as f:
        for i, linea in enumerate(f, start=1):
            linea = linea.strip()
            if not linea:
                continue
            try:
                filas.append(json.loads(linea))
            except json.JSONDecodeError as e:
                print(f"aviso: {ruta}:{i} no es JSON valido, se omite ({e})", file=sys.stderr)
    filas.sort(key=lambda r: r.get("data_ts", 0))
    return filas


def leer_perfil(ruta):
    """fase,multiplo_inicio,multiplo_fin,duracion_seg -> lista de dicts."""
    fases = []
    with open(ruta, newline="", encoding="utf-8") as f:
        for fila in csv.DictReader(f):
            fases.append({
                "fase": fila["fase"],
                "duracion_seg": int(fila["duracion_seg"]),
            })
    return fases


def construir_mapa_fases(fases, inicio_ts):
    """Devuelve una funcion ts(epoch) -> nombre de fase, o "" si ts cae fuera del
    experimento (antes de inicio_ts o despues de que termino la ultima fase)."""
    limites = []  # (inicio, fin, nombre), fin exclusivo
    acumulado = inicio_ts
    for f in fases:
        ini = acumulado
        fin = acumulado + f["duracion_seg"]
        limites.append((ini, fin, f["fase"]))
        acumulado = fin

    def fase_en(ts):
        for ini, fin, nombre in limites:
            if ini <= ts < fin:
                return nombre
        return ""

    return fase_en, acumulado  # acumulado = fin del experimento completo


def leer_run_info(ruta):
    """run_info.txt: lineas clave=valor. Devuelve el dict completo."""
    datos = {}
    with open(ruta, encoding="utf-8") as f:
        for linea in f:
            linea = linea.strip()
            if not linea or "=" not in linea:
                continue
            clave, _, valor = linea.partition("=")
            datos[clave.strip()] = valor.strip()
    return datos


def leer_p90(ruta):
    """JSON crudo de 'aws cloudwatch get-metric-data --query MetricDataResults[0]':
    {"Id": "p90", "Timestamps": [...], "Values": [...], ...}. TargetResponseTime viene
    en SEGUNDOS: se convierte a ms aqui. Devuelve dict ts_epoch_redondeado_al_minuto -> ms."""
    with open(ruta, encoding="utf-8") as f:
        datos = json.load(f)
    timestamps = datos.get("Timestamps", [])
    valores = datos.get("Values", [])
    mapa = {}
    for ts_texto, valor_seg in zip(timestamps, valores):
        try:
            ts = epoch(parse_iso(ts_texto))
        except ValueError:
            continue
        mapa[ts] = valor_seg * 1000.0
    return mapa


# --- Construccion del timeline ------------------------------------------------------

def necesarias(demanda_rpm, c_rpm, min_cap, max_cap):
    if demanda_rpm is None:
        return None
    import math
    crudas = math.ceil(demanda_rpm / c_rpm)
    return max(min_cap, min(max_cap, crudas))


def construir_timeline(decisiones, fase_en, p90_por_ts, cfg):
    filas = []
    for d in decisiones:
        ts = d.get("data_ts")
        if ts is None:
            continue
        demanda_rpm = d.get("request_count")
        healthy = d.get("healthy_hosts")
        filas.append({
            "ts": datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "ts_epoch": ts,
            "fase": fase_en(ts),
            "demanda_rpm": demanda_rpm,
            "forecast_rpm": d.get("forecast_rpm"),
            "cpu": d.get("cpu"),
            "ma_cpu": d.get("ma_cpu"),
            "necesarias_D": necesarias(demanda_rpm, cfg.c_rpm, cfg.min_capacity, cfg.max_capacity),
            "healthy_S": healthy,
            "desired": d.get("desired"),
            "pending": d.get("pending"),
            "p90_ms": p90_por_ts.get(ts),
            "decision": d.get("decision"),
            "trigger": d.get("trigger"),
        })
    return filas


COLUMNAS = ["ts", "fase", "demanda_rpm", "forecast_rpm", "cpu", "ma_cpu", "necesarias_D",
            "healthy_S", "desired", "pending", "p90_ms", "decision", "trigger"]


def escribir_csv(filas, ruta):
    with open(ruta, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNAS, extrasaction="ignore")
        w.writeheader()
        for fila in filas:
            w.writerow(fila)


# --- Resumen de evaluacion -----------------------------------------------------------

def episodios_por_encima(filas, get_d, get_s):
    """Episodios continuos donde D > S (subaprovisionado): tiempo de reaccion = desde
    que empieza el episodio hasta que S >= D. Un episodio que no cierra antes de que se
    acaben los datos se reporta como abierto (sin resolver)."""
    episodios = []
    abierto = None
    for fila in filas:
        d, s, ts = get_d(fila), get_s(fila), fila["ts_epoch"]
        if d is None or s is None:
            continue
        if d > s:
            if abierto is None:
                abierto = {"inicio": ts, "fin": None}
        else:
            if abierto is not None:
                abierto["fin"] = ts
                episodios.append(abierto)
                abierto = None
    if abierto is not None:
        episodios.append(abierto)  # sigue abierto al final de los datos
    return episodios


def episodios_de_descenso(filas, get_d, get_s):
    """Cada vez que D baja respecto al minuto anterior se abre un episodio de descenso;
    se cierra en el primer minuto en que S <= D (la nueva capacidad ya alcanzo)."""
    episodios = []
    abierto = None
    d_anterior = None
    for fila in filas:
        d, s, ts = get_d(fila), get_s(fila), fila["ts_epoch"]
        if d is None or s is None:
            d_anterior = d if d is not None else d_anterior
            continue
        if d_anterior is not None and d < d_anterior and abierto is None:
            abierto = {"inicio": ts, "fin": None}
        if abierto is not None and s <= d:
            abierto["fin"] = ts
            episodios.append(abierto)
            abierto = None
        d_anterior = d
    if abierto is not None:
        episodios.append(abierto)
    return episodios


def calcular_resumen(filas, cfg):
    en_experimento = [f for f in filas if f["fase"]]
    con_d_y_s = [f for f in en_experimento if f["necesarias_D"] is not None and f["healthy_S"] is not None]

    sub = sum(1 for f in con_d_y_s if f["healthy_S"] < f["necesarias_D"])
    sobre = sum(1 for f in con_d_y_s if f["healthy_S"] > f["necesarias_D"])
    justo = sum(1 for f in con_d_y_s if f["healthy_S"] == f["necesarias_D"])

    reaccion = episodios_por_encima(con_d_y_s, lambda f: f["necesarias_D"], lambda f: f["healthy_S"])
    descenso = episodios_de_descenso(con_d_y_s, lambda f: f["necesarias_D"], lambda f: f["healthy_S"])

    def duraciones_min(episodios):
        return [(e["fin"] - e["inicio"]) / 60.0 for e in episodios if e["fin"] is not None]

    dur_reaccion = duraciones_min(reaccion)
    dur_descenso = duraciones_min(descenso)

    adaptaciones = sum(1 for f in en_experimento if f["decision"] and f["decision"] != "MAINTAIN_CAPACITY")
    instancia_minutos = sum(f["desired"] for f in en_experimento if f["desired"] is not None)

    con_p90 = [f for f in en_experimento if f["p90_ms"] is not None]
    dentro_slo = sum(1 for f in con_p90 if f["p90_ms"] <= cfg.slo_ms)

    return {
        "minutos_en_experimento": len(en_experimento),
        "minutos_con_D_y_S": len(con_d_y_s),
        "minutos_subaprovisionado": sub,
        "minutos_sobreaprovisionado": sobre,
        "minutos_just_in_need": justo,
        "episodios_reaccion": len(reaccion),
        "episodios_reaccion_sin_resolver": sum(1 for e in reaccion if e["fin"] is None),
        "tiempo_reaccion_prom_min": round(sum(dur_reaccion) / len(dur_reaccion), 1) if dur_reaccion else None,
        "tiempo_reaccion_max_min": round(max(dur_reaccion), 1) if dur_reaccion else None,
        "episodios_descenso": len(descenso),
        "episodios_descenso_sin_resolver": sum(1 for e in descenso if e["fin"] is None),
        "tiempo_descenso_prom_min": round(sum(dur_descenso) / len(dur_descenso), 1) if dur_descenso else None,
        "tiempo_descenso_max_min": round(max(dur_descenso), 1) if dur_descenso else None,
        "adaptaciones": adaptaciones,
        "instancia_minutos": instancia_minutos,
        "minutos_con_p90": len(con_p90),
        "pct_minutos_dentro_slo": round(dentro_slo / len(con_p90) * 100, 1) if con_p90 else None,
    }


def imprimir_resumen(r):
    print("\n=== Resumen de evaluacion ===")
    print(f"Minutos en el experimento             : {r['minutos_en_experimento']}")
    print(f"  con D y S disponibles                : {r['minutos_con_D_y_S']}")
    print(f"  subaprovisionado (S < D)              : {r['minutos_subaprovisionado']}")
    print(f"  sobreaprovisionado (S > D)             : {r['minutos_sobreaprovisionado']}")
    print(f"  just-in-need (S = D)                   : {r['minutos_just_in_need']}")
    print(f"Episodios de reaccion (D>S -> S>=D)    : {r['episodios_reaccion']}"
          f" ({r['episodios_reaccion_sin_resolver']} sin resolver)")
    print(f"  tiempo de reaccion promedio/max (min)  : {r['tiempo_reaccion_prom_min']} / {r['tiempo_reaccion_max_min']}")
    print(f"Episodios de descenso (D baja -> S<=D) : {r['episodios_descenso']}"
          f" ({r['episodios_descenso_sin_resolver']} sin resolver)")
    print(f"  tiempo de descenso promedio/max (min)  : {r['tiempo_descenso_prom_min']} / {r['tiempo_descenso_max_min']}")
    print(f"Adaptaciones (cambios de capacidad)    : {r['adaptaciones']}")
    print(f"Instancia-minutos (costo)              : {r['instancia_minutos']}")
    if r["minutos_con_p90"]:
        print(f"% minutos con p90 <= SLO ({r['minutos_con_p90']} min con dato)  : {r['pct_minutos_dentro_slo']}%")
    else:
        print("% minutos con p90 <= SLO               : sin datos de p90 (no se paso --p90-json)")


# --- CLI -----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decisions", required=True, help="logs/decisions.jsonl")
    ap.add_argument("--scenario", required=True, help="perfil de fases usado en la corrida (csv)")
    ap.add_argument("--inicio-utc", help="timestamp ISO UTC de arranque del experimento (p. ej. 2026-09-24T12:00:00Z)")
    ap.add_argument("--run-info", help="run_info.txt escrito por scripts/k6-escenario.sh (alternativa a --inicio-utc)")
    ap.add_argument("--p90-json", help="export de CloudWatch con Timestamps/Values de TargetResponseTime (ver docstring)")
    ap.add_argument("--out", default="timeline.csv")
    ap.add_argument("--c-rpm", type=float, default=480.0)
    ap.add_argument("--min-capacity", type=int, default=1)
    ap.add_argument("--max-capacity", type=int, default=5)
    ap.add_argument("--slo-ms", type=float, default=500.0)
    cfg = ap.parse_args()

    if not cfg.inicio_utc and not cfg.run_info:
        ap.error("hace falta --inicio-utc o --run-info")

    if cfg.run_info:
        info = leer_run_info(cfg.run_info)
        inicio_texto = info.get("inicio_utc")
        if not inicio_texto:
            ap.error(f"{cfg.run_info} no tiene la clave inicio_utc")
    else:
        inicio_texto = cfg.inicio_utc
    inicio_ts = epoch(parse_iso(inicio_texto))

    decisiones = leer_decisiones(cfg.decisions)
    if not decisiones:
        print(f"aviso: {cfg.decisions} no tiene lineas validas", file=sys.stderr)

    fases = leer_perfil(cfg.scenario)
    fase_en, fin_ts = construir_mapa_fases(fases, inicio_ts)

    p90_por_ts = leer_p90(cfg.p90_json) if cfg.p90_json else {}

    filas = construir_timeline(decisiones, fase_en, p90_por_ts, cfg)
    escribir_csv(filas, cfg.out)
    print(f"Escrito {cfg.out} ({len(filas)} filas, experimento de "
          f"{inicio_texto} a {datetime.fromtimestamp(fin_ts, tz=timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')})")

    resumen = calcular_resumen(filas, cfg)
    imprimir_resumen(resumen)


if __name__ == "__main__":
    main()
