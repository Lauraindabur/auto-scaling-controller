#!/usr/bin/env bash
# =============================================================================
# k6-escenario.sh - Corre loadtest/k6/escenario_completo.js: el experimento
# completo del Paso 9 (rampa + pico + meseta + bajada + reposo), en lazo
# abierto, contra el DNS del ALB. El perfil de fases vive en
# loadtest/scenarios/escenario_completo.csv (o el que se pase con --scenario).
#
# Es la corrida que se usa para evaluar el controller de principio a fin:
# build_timeline.py y plot_timeline.py leen su resultado.
#
# Uso:
#   scripts/k6-escenario.sh --dns <host> [opciones]
#
# Opciones (o las variables de entorno indicadas):
#   --dns <host>            Destino: DNS del ALB o "localhost"      (LB_DNS)
#   --scenario <archivo>    Perfil de fases (csv)                   (SCENARIO_CSV)
#   --c-rpm <n>             Capacidad de 1 instancia, RPM            (C_RPM)
#                            Debe coincidir con C_RPM del controller.
#   --pre-vus <n>           VUs preasignadas                        (PRE_ALLOCATED_VUS)
#   --max-vus <n>           VUs maximas                             (MAX_VUS)
#   --req-timeout <dur>     Timeout de cada peticion (formato k6)   (REQ_TIMEOUT)
#   --allow-ip              Permite apuntar a una IP en vez de un DNS (ALLOW_IP)
#                            (no pasa por el Target Group, no sirve para probar
#                            el autoescalado, solo para calibrar una instancia)
#   --yes                   No pedir confirmacion
#   -h | --help
#
# Requisito: k6 (ver README, seccion "Pruebas de carga con k6").
# =============================================================================

set -uo pipefail

# ------------------------------- Configuracion --------------------------------
LB_DNS="${LB_DNS:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SCENARIO_CSV="${SCENARIO_CSV:-$REPO_ROOT/loadtest/scenarios/escenario_completo.csv}"
C_RPM="${C_RPM:-480}"
PRE_ALLOCATED_VUS="${PRE_ALLOCATED_VUS:-50}"
MAX_VUS="${MAX_VUS:-400}"
REQ_TIMEOUT="${REQ_TIMEOUT:-10s}"

K6_SCRIPT="$REPO_ROOT/loadtest/k6/escenario_completo.js"
RESULTS_ROOT="${RESULTS_ROOT:-$REPO_ROOT/results/k6-escenario}"

# ------------------------------- Utilidades -----------------------------------
ts() { date -u +%Y-%m-%dT%H:%M:%SZ; }
log() { echo "[$(ts)] $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

K6_PID=""
detener_k6() {
    if [[ -n "$K6_PID" ]] && kill -0 "$K6_PID" 2>/dev/null; then
        kill "$K6_PID" 2>/dev/null
        wait "$K6_PID" 2>/dev/null
    fi
    K6_PID=""
}
al_interrumpir() {
    echo
    detener_k6
    log "EXPERIMENTO DETENIDO (interrumpido por el usuario)."
    exit 130
}
trap al_interrumpir INT TERM
trap detener_k6 EXIT

# ----------------------------- Argumentos -------------------------------------
CONFIRMAR=1
ALLOW_IP="${ALLOW_IP:-0}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dns)          [[ $# -ge 2 ]] || die "--dns necesita un valor"; LB_DNS="$2"; shift 2 ;;
        --scenario)     [[ $# -ge 2 ]] || die "--scenario necesita un valor"; SCENARIO_CSV="$2"; shift 2 ;;
        --c-rpm)        [[ $# -ge 2 ]] || die "--c-rpm necesita un valor"; C_RPM="$2"; shift 2 ;;
        --pre-vus)      [[ $# -ge 2 ]] || die "--pre-vus necesita un valor"; PRE_ALLOCATED_VUS="$2"; shift 2 ;;
        --max-vus)      [[ $# -ge 2 ]] || die "--max-vus necesita un valor"; MAX_VUS="$2"; shift 2 ;;
        --req-timeout)  [[ $# -ge 2 ]] || die "--req-timeout necesita un valor"; REQ_TIMEOUT="$2"; shift 2 ;;
        --allow-ip)     ALLOW_IP=1; shift ;;
        --yes|-y)       CONFIRMAR=0; shift ;;
        -h|--help)      sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)              die "opcion desconocida: $1 (usa --help)" ;;
    esac
done

# ----------------------------- Validaciones -----------------------------------
command -v k6 >/dev/null 2>&1 || die "k6 no está instalado. Ver README, sección 'Pruebas de carga con k6'."
[[ -f "$K6_SCRIPT" ]] || die "no se encontró $K6_SCRIPT"
[[ -f "$SCENARIO_CSV" ]] || die "no se encontró el perfil de fases: $SCENARIO_CSV"

[[ -n "$LB_DNS" ]] || die "falta --dns (o la variable LB_DNS): DNS del ALB o 'localhost'"
LB_DNS="${LB_DNS#http://}"; LB_DNS="${LB_DNS#https://}"; LB_DNS="${LB_DNS%%/*}"
if [[ "$LB_DNS" =~ ^[0-9]+(\.[0-9]+){3}(:[0-9]+)?$ ]]; then
    if (( ! ALLOW_IP )); then
        die "'$LB_DNS' es una IP. Usa el DNS del Load Balancer, 'localhost', o pasa --allow-ip si de verdad quieres apuntar a una instancia suelta (no prueba el autoescalado)."
    fi
    echo "AVISO: apuntando directo a una IP ($LB_DNS), fuera del ALB. No pasa por el" >&2
    echo "       Target Group: no sirve para probar el autoescalado, solo calibracion." >&2
fi
TARGET_URL="http://${LB_DNS}"

[[ "$C_RPM" =~ ^[0-9]+(\.[0-9]+)?$ ]] && awk -v c="$C_RPM" 'BEGIN{exit !(c>0)}' || die "--c-rpm debe ser un numero > 0"
[[ "$PRE_ALLOCATED_VUS" =~ ^[0-9]+$ ]] || die "--pre-vus debe ser un entero"
[[ "$MAX_VUS" =~ ^[0-9]+$ ]] || die "--max-vus debe ser un entero"
(( MAX_VUS >= PRE_ALLOCATED_VUS )) || die "--max-vus no puede ser menor que --pre-vus"

# ----------------------------- Preflight ---------------------------------------
preflight() {
    log "Prueba rapida contra ${TARGET_URL}: 5x /health + 1x /"
    local ok=0 i code
    for i in 1 2 3 4 5; do
        code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 "${TARGET_URL}/health" || echo "000")
        [[ "$code" == "200" ]] && ok=$((ok + 1))
    done
    (( ok == 5 )) || die "/health no respondió 200 las 5 veces (ok=$ok/5). No se genera carga."
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 "${TARGET_URL}/" || echo "000")
    [[ "$code" == "200" ]] || die "/ no respondió 200 (código '$code'). No se genera carga."
    log "OK: ${TARGET_URL} responde en /health y /"
}
preflight

# ----------------------------- Plan y confirmacion ------------------------------
TOTAL_SEGUNDOS=$(awk -F',' 'NR>1 {s+=$4} END {print s+0}' "$SCENARIO_CSV")
NUM_FASES=$(( $(wc -l < "$SCENARIO_CSV") - 1 ))
echo "Plan del experimento completo (k6, lazo abierto, tasas en multiplos de C)"
echo "  Destino            : $TARGET_URL"
echo "  Perfil de fases     : $SCENARIO_CSV ($NUM_FASES fases)"
echo "  C_RPM (1 instancia) : $C_RPM RPM"
echo "  Duracion total      : ${TOTAL_SEGUNDOS}s (~$(( TOTAL_SEGUNDOS / 60 )) min)"
echo "  VUs                 : preasignadas=$PRE_ALLOCATED_VUS, maximas=$MAX_VUS"
echo "  Timeout por peticion: $REQ_TIMEOUT"
echo "  Resultados en       : $RESULTS_ROOT/<fecha UTC>/"
echo "  Ctrl+C detiene la corrida de inmediato."
if (( CONFIRMAR )); then
    read -r -p "Escribe 'si' para empezar: " resp
    [[ "$resp" == "si" ]] || die "cancelado"
fi

# ----------------------------- Ejecucion ---------------------------------------
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_DIR="$RESULTS_ROOT/$RUN_ID"
mkdir -p "$RUN_DIR" || die "no se pudo crear $RUN_DIR"
cp "$SCENARIO_CSV" "$RUN_DIR/escenario.csv"

{
    echo "run_id=$RUN_ID"
    echo "target_url=$TARGET_URL"
    echo "scenario_csv=$SCENARIO_CSV"
    echo "c_rpm=$C_RPM"
    echo "pre_allocated_vus=$PRE_ALLOCATED_VUS"
    echo "max_vus=$MAX_VUS"
    echo "req_timeout=$REQ_TIMEOUT"
    echo "inicio_utc=$(ts)"
} > "$RUN_DIR/run_info.txt"

log "===== EXPERIMENTO COMPLETO INICIO (~$(( TOTAL_SEGUNDOS / 60 )) min) ====="

TARGET_URL="$TARGET_URL" \
SCENARIO_CSV="$SCENARIO_CSV" \
C_RPM="$C_RPM" \
PRE_ALLOCATED_VUS="$PRE_ALLOCATED_VUS" \
MAX_VUS="$MAX_VUS" \
REQ_TIMEOUT="$REQ_TIMEOUT" \
RESULTS_DIR="$RUN_DIR" \
k6 run --out "csv=$RUN_DIR/raw.csv" "$K6_SCRIPT" > >(tee "$RUN_DIR/run.log") 2>&1 &
K6_PID=$!
wait "$K6_PID"
rc=$?
K6_PID=""
wait 2>/dev/null

echo "fin_utc=$(ts)" >> "$RUN_DIR/run_info.txt"
log "===== EXPERIMENTO COMPLETO FIN (código $rc) ====="
if (( rc != 0 )); then
    log "k6 terminó con error (código $rc). Revisa $RUN_DIR/run.log."
fi

echo
log "Resultados guardados en $RUN_DIR (raw.csv, summary.json, escenario.csv, run_info.txt, run.log)"
log "Siguiente paso: scripts/build_timeline.py --run-dir $RUN_DIR --decisions logs/decisions.jsonl --p90-csv <export de CloudWatch>"
exit "$rc"
