#!/usr/bin/env bash
# =============================================================================
# load-test.sh - Genera carga escalonada contra el Application Load Balancer
# para observar como reacciona el controller de autoescalado.
#
# Uso:
#   scripts/load-test.sh [run]  [opciones]   3 niveles de carga crecientes y luego fase de reposo
#   scripts/load-test.sh check  [opciones]   prueba rapida (10 requests, 1 conexion) sin cargar nada
#   scripts/load-test.sh idle   [opciones]   solo la fase de "bajar carga" (no genera trafico)
#
# Opciones (tambien se pueden dar por variable de entorno, ver abajo):
#   --dns <host>            DNS del Load Balancer      (LB_DNS)
#   --levels "2 8 16"       concurrencias por nivel    (LEVELS)
#   --duration <segundos>   duracion de cada nivel     (LEVEL_DURATION_SECONDS)
#   --idle-minutes <min>    duracion de la fase idle   (IDLE_MINUTES)
#   --yes                   no pedir confirmacion
#   -h | --help
#
# Requisito: Apache Bench.  sudo apt install apache2-utils
# =============================================================================

set -uo pipefail

# ------------------------------- Configuracion --------------------------------
# Siempre se apunta al DNS del ALB, nunca a la IP de una instancia: asi el ALB
# reparte el trafico y las instancias nuevas reciben carga cuando entran al
# Target Group (que es lo que se quiere observar).
LB_DNS="${LB_DNS:-controller-lb-273346900.us-east-1.elb.amazonaws.com}"

# Concurrencia (conexiones simultaneas de ab, contra el ALB completo) de cada nivel.
#
# Por que 2, 8 y 16:
#  - La app Flask hace un bucle de 2.000.000 de sumas por request. Es trabajo
#    puro de CPU en Python (un solo hilo efectivo por el GIL), del orden de
#    0.1-0.3 s por request en una t3.micro (ESTIMACION: se mide en la prueba
#    "check", que imprime el tiempo por request con 1 sola conexion).
#  - Con ab (carga "cerrada": cada conexion lanza la siguiente al terminar),
#    una instancia ya trabaja al maximo con 1-2 conexiones; a partir de ahi
#    mas concurrencia no da mas throughput, solo hace crecer la COLA, y el
#    tiempo de respuesta ~ concurrencia x tiempo_por_request.
#  - Con el umbral de TargetResponseTime en 1.0 s, el cruce ocurre cuando hay
#    mas de ~1 / tiempo_por_request conexiones por instancia (unas 4-10).
#      Nivel 1 -> c=2 : linea base. Latencia ~0.2-0.6 s, debajo del umbral.
#                       Se espera MAINTAIN_CAPACITY (comprueba que no hay
#                       falsos positivos).
#      Nivel 2 -> c=8 : con 1 instancia la latencia supera 1 s. Se espera
#                       INCREASE_CAPACITY por RT; al entrar la segunda
#                       instancia quedan 4 conexiones por instancia.
#      Nivel 3 -> c=16: con 2 instancias son 8 por instancia, otra vez sobre el
#                       umbral: se espera un segundo escalon hacia arriba.
#  - Aviso: como el proceso Flask usa ~1 vCPU y una t3.micro tiene 2, la
#    CPUUtilization de una instancia satura cerca del 50 %, por debajo del
#    umbral de 70. Por eso es probable que el escalado lo dispare el tiempo de
#    respuesta y no la CPU (ver "decision_trigger" en logs/decisions.jsonl).
#
# Limite de seguridad: nunca se permiten mas de MAX_CONCURRENCY conexiones.
# Con <= 16 conexiones y un servidor que atiende ~5-40 req/s en total, el
# trafico es minimo (muy por debajo de lo que AWS considera una prueba de
# estres), asi que no hay riesgo de bloqueo ni de tirar las instancias. Ademas
# el script se detiene solo si el ALB devuelve muchos errores (502/503/504...).
LEVELS="${LEVELS:-2 8 16}"
MAX_CONCURRENCY=30          # tope duro, no se puede superar por parametro

# Duracion de cada nivel: 600 s (10 min).
# El controller evalua cada 60 s con media movil de 3 muestras, asi que necesita
# ~3 ciclos para que la MA refleje el nivel (mas 1-2 min de retraso de
# CloudWatch). Despues, lanzar la instancia y pasar los health checks tarda
# ~2-3 min, y luego hay 3 ciclos de cooldown. Con 10 min por nivel da tiempo a
# ver como minimo una decision de subida por nivel.
LEVEL_DURATION_SECONDS="${LEVEL_DURATION_SECONDS:-600}"
MIN_DURATION=60
MAX_DURATION=3600

# Fase de reposo ("bajar la carga"): no se manda nada. Con CPU ~0 y RT = 0, el
# controller deberia ir reduciendo de a 1 instancia; cada reduccion pide
# confirmacion + 3 ciclos de cooldown (~5 min), asi que volver de 4-5
# instancias a 1 puede tardar mas de 20 min.
IDLE_MINUTES="${IDLE_MINUTES:-20}"

REQUEST_TIMEOUT=30          # segundos que ab espera una respuesta (ab -s)
MAX_ERROR_PERCENT=20        # si mas del 20 % de un nivel son errores, se aborta

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_ROOT="${RESULTS_ROOT:-$SCRIPT_DIR/../results/load-test}"

# ------------------------------- Utilidades -----------------------------------
# Marcas de tiempo UTC con el mismo formato que logs/decisions.jsonl.
ts() { date -u +%Y-%m-%dT%H:%M:%SZ; }
log() { echo "[$(ts)] $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

AB_PID=""

detener_generador() {
    if [[ -n "$AB_PID" ]] && kill -0 "$AB_PID" 2>/dev/null; then
        kill "$AB_PID" 2>/dev/null
        wait "$AB_PID" 2>/dev/null
    fi
    AB_PID=""
}

al_interrumpir() {
    echo
    detener_generador
    log "GENERADOR DETENIDO (interrumpido por el usuario). Carga = 0 desde este instante."
    exit 130
}
trap al_interrumpir INT TERM
trap detener_generador EXIT

es_entero() { [[ "$1" =~ ^[0-9]+$ ]]; }

# ----------------------------- Argumentos -------------------------------------
COMANDO="run"
CONFIRMAR=1

if [[ $# -gt 0 && "$1" != -* ]]; then
    COMANDO="$1"; shift
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dns)          [[ $# -ge 2 ]] || die "--dns necesita un valor"; LB_DNS="$2"; shift 2 ;;
        --levels)       [[ $# -ge 2 ]] || die "--levels necesita un valor"; LEVELS="$2"; shift 2 ;;
        --duration)     [[ $# -ge 2 ]] || die "--duration necesita un valor"; LEVEL_DURATION_SECONDS="$2"; shift 2 ;;
        --idle-minutes) [[ $# -ge 2 ]] || die "--idle-minutes necesita un valor"; IDLE_MINUTES="$2"; shift 2 ;;
        --yes|-y)       CONFIRMAR=0; shift ;;
        -h|--help)      sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)              die "opcion desconocida: $1 (usa --help)" ;;
    esac
done

case "$COMANDO" in run|check|idle) ;; *) die "comando desconocido: $COMANDO (usa run, check o idle)" ;; esac

# ----------------------------- Validaciones -----------------------------------
# El destino debe ser un DNS, no una IP. Se admite que pasen "http://..." y se quita.
LB_DNS="${LB_DNS#http://}"; LB_DNS="${LB_DNS#https://}"; LB_DNS="${LB_DNS%%/*}"
[[ -n "$LB_DNS" ]] || die "LB_DNS vacio"
if [[ "$LB_DNS" =~ ^[0-9]+(\.[0-9]+){3}(:[0-9]+)?$ ]]; then
    die "'$LB_DNS' es una IP. Este script solo apunta al DNS del Load Balancer, nunca a una instancia."
fi
[[ "$LB_DNS" == *.elb.amazonaws.com* ]] || \
    echo "AVISO: '$LB_DNS' no parece un DNS de ELB (*.elb.amazonaws.com); verifica que sea el Load Balancer." >&2
URL="http://${LB_DNS}/"

es_entero "$LEVEL_DURATION_SECONDS" || die "--duration debe ser un entero (segundos)"
(( LEVEL_DURATION_SECONDS >= MIN_DURATION && LEVEL_DURATION_SECONDS <= MAX_DURATION )) || \
    die "la duracion por nivel debe estar entre $MIN_DURATION y $MAX_DURATION segundos"
es_entero "$IDLE_MINUTES" || die "--idle-minutes debe ser un entero (minutos)"

read -r -a NIVELES <<< "$LEVELS"
(( ${#NIVELES[@]} >= 1 )) || die "hay que dar al menos un nivel"
for c in "${NIVELES[@]}"; do
    es_entero "$c" || die "nivel de concurrencia invalido: '$c'"
    (( c >= 1 && c <= MAX_CONCURRENCY )) || \
        die "concurrencia $c fuera de rango (1-$MAX_CONCURRENCY). El tope existe para no saturar las instancias."
done
(( ${#NIVELES[@]} >= 3 )) || echo "AVISO: menos de 3 niveles; no alcanza para graficar una curva de carga." >&2

command -v ab >/dev/null 2>&1 || die "Apache Bench (ab) no esta instalado. Instalalo con:  sudo apt update && sudo apt install -y apache2-utils"

# ----------------------------- Funciones --------------------------------------
# Latido cada 60 s mientras corre el generador.
esperar_generador() {
    local inicio=$SECONDS
    while kill -0 "$AB_PID" 2>/dev/null; do
        sleep 1
        if (( (SECONDS - inicio) % 60 == 0 && SECONDS > inicio )); then
            log "  ... $(( (SECONDS - inicio) / 60 ))/$(( LEVEL_DURATION_SECONDS / 60 )) min transcurridos"
        fi
    done
    wait "$AB_PID"
    local rc=$?
    AB_PID=""
    return $rc
}

# Extrae un valor numerico de la salida de ab (0 si no aparece).
valor_ab() { # archivo, patron_grep, columna_awk
    local v
    v=$(grep -m1 -E "$2" "$1" | awk -v col="$3" '{print $col}')
    echo "${v:-0}"
}

# Prueba corta: comprueba que el ALB responde y mide el tiempo por request sin carga.
preflight() {
    local salida="$1"
    log "Prueba rapida contra $URL (10 requests, 1 conexion)"
    ab -n 10 -c 1 -s "$REQUEST_TIMEOUT" "$URL" > "$salida" 2>&1 || {
        cat "$salida"; die "ab no pudo completar la prueba rapida contra $URL"; }
    local fallidas non2xx media
    fallidas=$(valor_ab "$salida" "^Failed requests:" 3)
    non2xx=$(valor_ab "$salida" "^Non-2xx responses:" 3)
    media=$(valor_ab "$salida" "^Time per request:.*\(mean\)$" 4)
    (( fallidas == 0 && non2xx == 0 )) || { cat "$salida"; die "la prueba rapida tuvo errores (fallidas=$fallidas, non-2xx=$non2xx); no se genera carga."; }
    log "OK: el Load Balancer responde. Tiempo medio por request con 1 conexion: ${media} ms"
    log "    (referencia: latencia con c conexiones sobre 1 instancia ~ c x ${media} ms)"
}

ejecutar_nivel() {
    local idx="$1" c="$2"
    local base="$RUN_DIR/level_${idx}_c${c}"
    local inicio fin
    inicio=$(ts)
    echo
    log "===== NIVEL $idx INICIO: concurrencia=$c duracion=${LEVEL_DURATION_SECONDS}s ====="
    # -t: limite de tiempo (implica un maximo de 50000 requests, muy por encima de lo esperado)
    # -g / -e: TSV para gnuplot y CSV de percentiles, para graficar despues
    ab -c "$c" -t "$LEVEL_DURATION_SECONDS" -s "$REQUEST_TIMEOUT" \
       -g "$base.tsv" -e "$base.csv" "$URL" > "$base.txt" 2>&1 &
    AB_PID=$!
    esperar_generador
    local rc=$?
    fin=$(ts)
    log "===== NIVEL $idx FIN: concurrencia=$c (generador detenido) ====="
    echo "--- salida de ab (nivel $idx, c=$c) ---"
    cat "$base.txt"
    echo "--- fin salida de ab ---"

    local completas fallidas non2xx rps media p50 p95 p99
    completas=$(valor_ab "$base.txt" "^Complete requests:" 3)
    fallidas=$(valor_ab "$base.txt" "^Failed requests:" 3)
    non2xx=$(valor_ab "$base.txt" "^Non-2xx responses:" 3)
    rps=$(valor_ab "$base.txt" "^Requests per second:" 4)
    media=$(valor_ab "$base.txt" "^Time per request:.*\(mean\)$" 4)
    p50=$(valor_ab "$base.txt" "^ +50%" 2)
    p95=$(valor_ab "$base.txt" "^ +95%" 2)
    p99=$(valor_ab "$base.txt" "^ +99%" 2)
    echo "$idx,$c,$inicio,$fin,$LEVEL_DURATION_SECONDS,$completas,$fallidas,$non2xx,$rps,$media,$p50,$p95,$p99" >> "$SUMMARY"

    if (( rc != 0 )); then
        log "ab termino con error (codigo $rc). Se detiene la secuencia."
        return 1
    fi
    # Proteccion: si el ALB/instancias devuelven muchos errores, no se sigue apretando.
    local errores=$(( fallidas > non2xx ? fallidas : non2xx ))
    if (( completas == 0 )) || (( errores * 100 > completas * MAX_ERROR_PERCENT )); then
        log "DEMASIADOS ERRORES (completas=$completas, fallidas=$fallidas, non-2xx=$non2xx). Se detiene la secuencia para no seguir cargando."
        return 1
    fi
    return 0
}

fase_reposo() {
    local minutos="$1"
    echo
    log "===== FASE DE REPOSO INICIO: GENERADOR DETENIDO, no se envia trafico (carga = 0) ====="
    log "Ahora se observa el escalado hacia ABAJO. En otra terminal:"
    log "  tail -f logs/decisions.jsonl | jq -c '[.timestamp,.moving_average_cpu,.moving_average_response_time,.current_capacity,.decision,.decision_trigger,.justification]'"
    local inicio=$SECONDS total=$(( minutos * 60 ))
    while (( SECONDS - inicio < total )); do
        sleep 60
        log "  ... reposo: $(( (SECONDS - inicio) / 60 ))/${minutos} min"
    done
    log "===== FASE DE REPOSO FIN ====="
}

# ----------------------------- Ejecucion --------------------------------------
if [[ "$COMANDO" == "check" ]]; then
    mkdir -p "$RESULTS_ROOT"
    preflight "$(mktemp)"
    exit 0
fi

if [[ "$COMANDO" == "idle" ]]; then
    (( IDLE_MINUTES >= 1 )) || die "--idle-minutes debe ser >= 1"
    fase_reposo "$IDLE_MINUTES"
    exit 0
fi

# --- run ---
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_DIR="$RESULTS_ROOT/$RUN_ID"
SUMMARY="$RUN_DIR/summary.csv"
TOTAL_SEG=$(( ${#NIVELES[@]} * LEVEL_DURATION_SECONDS + IDLE_MINUTES * 60 ))

echo "Plan de la prueba de carga"
echo "  Destino (ALB)      : $URL"
echo "  Niveles (concurr.) : ${NIVELES[*]}"
echo "  Duracion por nivel : ${LEVEL_DURATION_SECONDS}s"
echo "  Fase de reposo     : ${IDLE_MINUTES} min (sin trafico)"
echo "  Duracion total     : ~$(( TOTAL_SEG / 60 )) min"
echo "  Resultados en      : $RUN_DIR"
echo "  Tope de seguridad  : ${MAX_CONCURRENCY} conexiones; aborta si >${MAX_ERROR_PERCENT}% de errores"
echo "  Ctrl+C detiene el generador de inmediato."
if (( CONFIRMAR )); then
    read -r -p "Escribe 'si' para empezar: " resp
    [[ "$resp" == "si" ]] || die "cancelado"
fi

mkdir -p "$RUN_DIR" || die "no se pudo crear $RUN_DIR"
echo "nivel,concurrencia,inicio_utc,fin_utc,duracion_s,completas,fallidas,non2xx,req_por_seg,ms_por_request_medio,p50_ms,p95_ms,p99_ms" > "$SUMMARY"
{
    echo "run_id=$RUN_ID"
    echo "destino=$URL"
    echo "niveles=${NIVELES[*]}"
    echo "duracion_nivel_s=$LEVEL_DURATION_SECONDS"
    echo "reposo_min=$IDLE_MINUTES"
    echo "inicio_utc=$(ts)"
} > "$RUN_DIR/run_info.txt"

preflight "$RUN_DIR/00_preflight.txt"

idx=0
completo=1
for c in "${NIVELES[@]}"; do
    idx=$(( idx + 1 ))
    ejecutar_nivel "$idx" "$c" || { completo=0; break; }
done

echo "fin_carga_utc=$(ts)" >> "$RUN_DIR/run_info.txt"
log "GENERADOR DETENIDO: la carga vuelve a 0."
(( IDLE_MINUTES >= 1 )) && fase_reposo "$IDLE_MINUTES"
echo "fin_utc=$(ts)" >> "$RUN_DIR/run_info.txt"

echo
log "Resultados guardados en $RUN_DIR (summary.csv, level_*_c*.txt/.tsv/.csv)"
(( completo )) || { log "La secuencia se detuvo antes de completar todos los niveles."; exit 1; }
