#!/usr/bin/env python3
"""Grafica timeline.csv (salida de build_timeline.py) como un PNG de 4 paneles
(Paso 9), con el mismo eje de tiempo y franjas de fondo por fase:

  1. Demanda: RPM real y pronostico de Holt.
  2. CPU: cruda, MA(3), lineas de umbral 70/30; zona MA > 70% sombreada.
  3. Capacidad: D (necesarias, escalonada), S (healthy) y desired; sombreado por
     sub/sobre-aprovisionamiento; marcadores de INCREASE/REDUCE coloreados por trigger.
  4. Latencia p90 con la linea del SLO.

Paleta: los 3 primeros slots categoricos de la paleta de referencia del proyecto
(azul/naranja/aqua) son los unicos que validan TODAS las combinaciones simultaneas
(references/palette.md de la skill dataviz); se usan solo ahi donde hacen falta 3
identidades a la vez (los marcadores de trigger). El resto de lineas (D, S, desired,
umbrales, SLO) van en tonos de tinta neutra, no colores categoricos: no son
identidades que haya que distinguir por color, son cantidades/referencias.
Los colores de estado (rojo/amarillo) quedan reservados solo para el sombreado de
sub/sobre-aprovisionamiento, nunca como color de una serie.

Requiere matplotlib (pip install matplotlib). El resto es biblioteca estandar.

Uso:
    python3 scripts/plot_timeline.py timeline.csv [--out timeline.png] [--slo-ms 500]
"""
import argparse
import csv
from datetime import datetime, timezone

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.lines import Line2D

# --- Paleta (references/palette.md de la skill dataviz) ----------------------------
INK_PRIMARY = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#8a8983"
GRID = "#e4e3de"

CAT_BLUE = "#2a78d6"     # slot 1
CAT_ORANGE = "#eb6834"   # slot 2
CAT_AQUA = "#1baf7a"     # slot 3

STATUS_CRITICAL = "#d03b3b"
STATUS_WARNING = "#fab219"

COLOR_TRIGGER = {"REACTIVE": CAT_BLUE, "PROACTIVE": CAT_ORANGE, "BOTH": CAT_AQUA}

FASE_BANDA_A = "#00000000"   # transparente
FASE_BANDA_B = "#0000000a"   # gris muy tenue (~4% negro)


# --- Lectura -------------------------------------------------------------------------

def a_float(v):
    if v is None or v == "":
        return None
    return float(v)


def a_int(v):
    if v is None or v == "":
        return None
    return int(float(v))


def leer_timeline(ruta):
    filas = []
    with open(ruta, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            t = datetime.strptime(r["ts"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
            filas.append({
                "t": t,
                "fase": r["fase"],
                "demanda_rpm": a_float(r["demanda_rpm"]),
                "forecast_rpm": a_float(r["forecast_rpm"]),
                "cpu": a_float(r["cpu"]),
                "ma_cpu": a_float(r["ma_cpu"]),
                "necesarias_D": a_int(r["necesarias_D"]),
                "healthy_S": a_float(r["healthy_S"]),
                "desired": a_int(r["desired"]),
                "pending": a_int(r["pending"]),
                "p90_ms": a_float(r["p90_ms"]),
                "decision": r["decision"],
                "trigger": r["trigger"],
            })
    return filas


def bandas_de_fase(filas):
    """Agrupa filas consecutivas con la misma fase en (inicio, fin, nombre)."""
    bandas = []
    actual = None
    for f in filas:
        if actual is None or f["fase"] != actual["fase"]:
            if actual is not None:
                bandas.append(actual)
            actual = {"fase": f["fase"], "inicio": f["t"], "fin": f["t"]}
        else:
            actual["fin"] = f["t"]
    if actual is not None:
        bandas.append(actual)
    return bandas


# --- Dibujo ----------------------------------------------------------------------------

def pintar_fases(ax, bandas, con_etiqueta=False):
    for i, b in enumerate(bandas):
        if not b["fase"]:
            continue
        color = FASE_BANDA_B if i % 2 == 1 else FASE_BANDA_A
        ax.axvspan(b["inicio"], b["fin"], color=color, zorder=0)
        ax.axvline(b["inicio"], color=INK_MUTED, lw=0.6, ls=":", alpha=0.6, zorder=1)
        if con_etiqueta:
            centro = b["inicio"] + (b["fin"] - b["inicio"]) / 2
            ax.annotate(b["fase"], xy=(centro, 1.0), xycoords=("data", "axes fraction"),
                        xytext=(0, 4), textcoords="offset points",
                        ha="center", va="bottom", fontsize=7.5, color=INK_SECONDARY)


def panel_demanda(ax, filas):
    t = [f["t"] for f in filas]
    ax.plot(t, [f["demanda_rpm"] for f in filas], color=CAT_BLUE, lw=2, label="Demanda real (RPM)")
    ax.plot(t, [f["forecast_rpm"] for f in filas], color=CAT_ORANGE, lw=1.6, ls="--",
            label="Pronostico Holt (RPM)")
    ax.set_ylabel("RPM")
    ax.legend(loc="upper left", fontsize=8, frameon=False)
    ax.grid(axis="y", color=GRID, lw=0.7)


def panel_cpu(ax, filas, umbral_alto=70.0, umbral_bajo=30.0):
    t = [f["t"] for f in filas]
    cpu = [f["cpu"] for f in filas]
    ma = [f["ma_cpu"] for f in filas]

    # Zona MA_CPU > umbral_alto sombreada (status critical, tenue): recorre tramos
    # contiguos por encima del umbral y los pinta como banda.
    en_zona = False
    ini = None
    for f in filas + [{"t": t[-1], "ma_cpu": None}]:
        activo = f["ma_cpu"] is not None and f["ma_cpu"] > umbral_alto
        if activo and not en_zona:
            ini, en_zona = f["t"], True
        elif not activo and en_zona:
            ax.axvspan(ini, f["t"], color=STATUS_CRITICAL, alpha=0.08, zorder=0)
            en_zona = False

    ax.plot(t, cpu, color=CAT_BLUE, lw=1, alpha=0.35, label="CPU cruda")
    ax.plot(t, ma, color=CAT_BLUE, lw=2, label="MA_CPU (3 muestras)")
    ax.axhline(umbral_alto, color=INK_SECONDARY, lw=1, ls="--")
    ax.axhline(umbral_bajo, color=INK_SECONDARY, lw=1, ls="--")
    ax.annotate(f"UMBRAL_ALTO={umbral_alto:.0f}%", xy=(t[-1], umbral_alto), xytext=(4, 2),
                textcoords="offset points", fontsize=7, color=INK_SECONDARY, ha="left")
    ax.annotate(f"UMBRAL_BAJO={umbral_bajo:.0f}%", xy=(t[-1], umbral_bajo), xytext=(4, 2),
                textcoords="offset points", fontsize=7, color=INK_SECONDARY, ha="left")
    ax.set_ylabel("CPU (%)")
    ax.set_ylim(0, 105)
    ax.legend(loc="upper left", fontsize=8, frameon=False)
    ax.grid(axis="y", color=GRID, lw=0.7)


def panel_capacidad(ax, filas):
    t = [f["t"] for f in filas]
    D = [f["necesarias_D"] for f in filas]
    S = [f["healthy_S"] for f in filas]
    desired = [f["desired"] for f in filas]

    # Sombreado por sub/sobre-aprovisionamiento: rojo si S<D, amarillo si S>D.
    for i in range(len(filas) - 1):
        d, s = D[i], S[i]
        if d is None or s is None:
            continue
        color = STATUS_CRITICAL if s < d else (STATUS_WARNING if s > d else None)
        if color:
            ax.axvspan(t[i], t[i + 1], color=color, alpha=0.12 if color == STATUS_CRITICAL else 0.15, zorder=0)

    ax.step(t, D, where="post", color=INK_PRIMARY, lw=2, label="D (necesarias)")
    ax.step(t, S, where="post", color=INK_SECONDARY, lw=1.6, label="S (healthy)")
    ax.plot(t, desired, color=INK_MUTED, lw=1.2, ls=":", label="desired")

    # Marcadores INCREASE/REDUCE, coloreados por trigger.
    for f in filas:
        if f["decision"] == "INCREASE_CAPACITY":
            ax.scatter([f["t"]], [f["desired"]], marker="^", s=60,
                       color=COLOR_TRIGGER.get(f["trigger"], INK_MUTED), zorder=5,
                       edgecolors="white", linewidths=0.6)
        elif f["decision"] == "REDUCE_CAPACITY":
            ax.scatter([f["t"]], [f["desired"]], marker="v", s=60,
                       color=COLOR_TRIGGER.get(f["trigger"], INK_MUTED), zorder=5,
                       edgecolors="white", linewidths=0.6)

    ax.set_ylabel("Instancias")
    lineas = ax.legend(loc="upper left", fontsize=8, frameon=False)
    ax.add_artist(lineas)
    marcadores = [
        Line2D([0], [0], marker="^", color="w", markerfacecolor=COLOR_TRIGGER["REACTIVE"], markersize=8, label="REACTIVE"),
        Line2D([0], [0], marker="^", color="w", markerfacecolor=COLOR_TRIGGER["PROACTIVE"], markersize=8, label="PROACTIVE"),
        Line2D([0], [0], marker="^", color="w", markerfacecolor=COLOR_TRIGGER["BOTH"], markersize=8, label="BOTH"),
    ]
    ax.legend(handles=marcadores, loc="upper right", fontsize=7.5, frameon=False,
              title="trigger (▲ sube / ▼ baja)", title_fontsize=7.5)
    ax.grid(axis="y", color=GRID, lw=0.7)


def panel_latencia(ax, filas, slo_ms=500.0):
    t = [f["t"] for f in filas]
    p90 = [f["p90_ms"] for f in filas]
    if all(v is None for v in p90):
        ax.text(0.5, 0.5, "sin datos de p90 (no se paso --p90-json a build_timeline.py)",
                transform=ax.transAxes, ha="center", va="center", fontsize=9, color=INK_MUTED)
        ax.set_ylabel("p90 (ms)")
        return
    por_encima = [v is not None and v > slo_ms for v in p90]
    en_zona, ini = False, None
    for i, activo in enumerate(por_encima + [False]):
        if activo and not en_zona:
            ini, en_zona = t[i], True
        elif not activo and en_zona:
            ax.axvspan(ini, t[min(i, len(t) - 1)], color=STATUS_CRITICAL, alpha=0.08, zorder=0)
            en_zona = False
    ax.plot(t, p90, color=CAT_BLUE, lw=2, label="p90 TargetResponseTime")
    ax.axhline(slo_ms, color=INK_SECONDARY, lw=1, ls="--")
    ax.annotate(f"SLO={slo_ms:.0f} ms", xy=(t[-1], slo_ms), xytext=(4, 2),
                textcoords="offset points", fontsize=7, color=INK_SECONDARY, ha="left")
    ax.set_ylabel("p90 (ms)")
    ax.legend(loc="upper left", fontsize=8, frameon=False)
    ax.grid(axis="y", color=GRID, lw=0.7)


def graficar(filas, ruta_salida, slo_ms, umbral_alto, umbral_bajo):
    fig, ejes = plt.subplots(4, 1, figsize=(13, 12), sharex=True,
                             gridspec_kw={"hspace": 0.15})
    fig.patch.set_facecolor("#fcfcfb")
    bandas = bandas_de_fase(filas)

    panel_demanda(ejes[0], filas)
    panel_cpu(ejes[1], filas, umbral_alto, umbral_bajo)
    panel_capacidad(ejes[2], filas)
    panel_latencia(ejes[3], filas, slo_ms)

    for i, ax in enumerate(ejes):
        ax.set_facecolor("#fcfcfb")
        for spine in ("top", "right"):
            ax.spines[spine].set_visible(False)
        for spine in ("left", "bottom"):
            ax.spines[spine].set_color(GRID)
        pintar_fases(ax, bandas, con_etiqueta=(i == 0))

    ejes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M", tz=timezone.utc))
    ejes[-1].set_xlabel("Hora UTC")
    fig.suptitle("Experimento completo: demanda, CPU, capacidad y latencia", fontsize=13, y=0.965)
    fig.subplots_adjust(top=0.90, bottom=0.05, hspace=0.28)

    fig.savefig(ruta_salida, dpi=150, bbox_inches="tight")
    print(f"Escrito {ruta_salida}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("timeline_csv")
    ap.add_argument("--out", default="timeline.png")
    ap.add_argument("--slo-ms", type=float, default=500.0)
    ap.add_argument("--umbral-alto", type=float, default=70.0)
    ap.add_argument("--umbral-bajo", type=float, default=30.0)
    args = ap.parse_args()

    filas = leer_timeline(args.timeline_csv)
    if not filas:
        raise SystemExit(f"{args.timeline_csv} no tiene filas")
    graficar(filas, args.out, args.slo_ms, args.umbral_alto, args.umbral_bajo)


if __name__ == "__main__":
    main()
