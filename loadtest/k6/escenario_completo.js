// Experimento completo del Paso 9: un solo escenario ramping-arrival-rate (lazo abierto)
// que recorre las fases de loadtest/scenarios/escenario_completo.csv. Las tasas se
// expresan en MULTIPLOS de C (la capacidad de una instancia, seccion 8), para que el
// perfil siga siendo el mismo aunque C_RPM se recalibre.
//
// El perfil no vive en este archivo: se lee de un CSV para poder ajustarlo sin tocar
// codigo. Cada fila es una fase: cuanto dura y entre que multiplos de C se mueve. Si
// multiplo_inicio == multiplo_fin la fase es plana; si son distintos, k6 interpola la
// tasa de forma lineal a lo largo de la fase (es literalmente la fase "rampa").
//
// Variables de entorno (todas con valor por defecto salvo TARGET_URL):
//   TARGET_URL          obligatoria. Ej: http://controller-lb-xxx.us-east-1.elb.amazonaws.com
//   SCENARIO_CSV        "loadtest/scenarios/escenario_completo.csv"  ruta al perfil
//   C_RPM               480         capacidad de una instancia, en peticiones por minuto
//                                   (debe coincidir con C_RPM del controller para que las
//                                   fases signifiquen lo que dicen: "meseta a 3xC" etc.)
//   PRE_ALLOCATED_VUS   50          VUs reservadas al arrancar
//   MAX_VUS             400         VUs maximas (el pico de la meseta, 3xC, mas la
//                                   latencia bajo saturacion, puede pedir bastantes)
//   REQ_TIMEOUT         "10s"       timeout de cada peticion HTTP
//   RESULTS_DIR         "."         carpeta donde handleSummary escribe summary.json
//
// Salida: raw.csv (via "k6 run --out csv=..."), RESULTS_DIR/summary.json, y el resumen de
// consola normal. build_timeline.py NO necesita raw.csv para la demanda (usa
// logs/decisions.jsonl, que es lo que el ALB realmente midio); raw.csv sirve para revisar
// la latencia y los errores vistos del lado del cliente.

import http from 'k6/http';
import { check } from 'k6';
import { textSummary } from 'https://jslib.k6.io/k6-summary/0.0.4/index.js';

// ---- Lectura y validacion de variables de entorno -------------------------------

const TARGET_URL = __ENV.TARGET_URL;
if (!TARGET_URL) {
  throw new Error('TARGET_URL es obligatoria, por ejemplo: -e TARGET_URL=http://mi-alb.elb.amazonaws.com');
}

const SCENARIO_CSV = __ENV.SCENARIO_CSV || 'loadtest/scenarios/escenario_completo.csv';
const C_RPM = parseFloat(__ENV.C_RPM || '480');
if (!Number.isFinite(C_RPM) || C_RPM <= 0) {
  throw new Error(`C_RPM invalido: "${__ENV.C_RPM}"`);
}
const RATE_POR_C = C_RPM / 60; // req/s que representa multiplo=1.0

const PRE_ALLOCATED_VUS = parseInt(__ENV.PRE_ALLOCATED_VUS || '50', 10);
const MAX_VUS = parseInt(__ENV.MAX_VUS || '400', 10);
const REQ_TIMEOUT = __ENV.REQ_TIMEOUT || '10s';
const RESULTS_DIR = __ENV.RESULTS_DIR || '.';

// ---- Perfil: CSV minimo, sin dependencias (fase,multiplo_inicio,multiplo_fin,duracion_seg)

function leerPerfil(ruta) {
  const texto = open(ruta);
  const lineas = texto.split('\n').map((l) => l.trim()).filter((l) => l.length > 0);
  if (lineas.length < 2) {
    throw new Error(`${ruta}: se esperaba una cabecera y al menos una fila de fase`);
  }
  const cabecera = lineas[0].split(',').map((c) => c.trim());
  const esperada = ['fase', 'multiplo_inicio', 'multiplo_fin', 'duracion_seg', 'salto_intencional'];
  if (cabecera.join(',') !== esperada.join(',')) {
    throw new Error(`${ruta}: cabecera inesperada "${cabecera.join(',')}", se esperaba "${esperada.join(',')}"`);
  }

  const fases = lineas.slice(1).map((linea, i) => {
    const cols = linea.split(',').map((c) => c.trim());
    if (cols.length !== 5) {
      throw new Error(`${ruta}: fila ${i + 2} con ${cols.length} columnas, se esperaban 5: "${linea}"`);
    }
    const [fase, multiploInicio, multiploFin, duracionSeg, saltoIntencionalStr] = cols;
    const mi = parseFloat(multiploInicio);
    const mf = parseFloat(multiploFin);
    const dur = parseInt(duracionSeg, 10);
    const saltoIntencional = saltoIntencionalStr === '1';
    if (!Number.isFinite(mi) || mi < 0) throw new Error(`${ruta}: fila ${i + 2} ("${fase}"): multiplo_inicio invalido`);
    if (!Number.isFinite(mf) || mf < 0) throw new Error(`${ruta}: fila ${i + 2} ("${fase}"): multiplo_fin invalido`);
    if (!Number.isInteger(dur) || dur <= 0) throw new Error(`${ruta}: fila ${i + 2} ("${fase}"): duracion_seg invalida`);
    return { fase, multiploInicio: mi, multiploFin: mf, duracionSeg: dur, saltoIntencional };
  });

  // Validar continuidad entre fases. Los saltos bruscos son permitidos solo si
  // la fase actual declara salto_intencional=1.
  for (let i = 1; i < fases.length; i++) {
    const prev = fases[i - 1];
    const cur = fases[i];
    const hayDiscontinuidad = Math.abs(prev.multiploFin - cur.multiploInicio) > 1e-9;
    if (hayDiscontinuidad && !cur.saltoIntencional) {
      throw new Error(
        `${ruta}: la fase "${cur.fase}" empieza en ${cur.multiploInicio}x pero "${prev.fase}" termino en ` +
        `${prev.multiploFin}x. Deberian coincidir, o declarar salto_intencional=1 en la fila de "${cur.fase}".`
      );
    }
  }
  return fases;
}

const FASES = leerPerfil(SCENARIO_CSV);

// ---- Arma el executor ramping-arrival-rate a partir de las fases -----------------

const startRate = Math.max(1, Math.round(FASES[0].multiploInicio * RATE_POR_C)); // k6 exige int >= 1
const stages = FASES.map((f) => ({
  target: Math.max(1, Math.round(f.multiploFin * RATE_POR_C)),
  duration: `${f.duracionSeg}s`,
}));

export const options = {
  scenarios: {
    experimento_completo: {
      executor: 'ramping-arrival-rate',
      startRate,
      timeUnit: '1s',
      preAllocatedVUs: PRE_ALLOCATED_VUS,
      maxVUs: MAX_VUS,
      stages,
      gracefulStop: '30s',
      exec: 'peticion',
    },
  },
  // La saturacion durante "pico" y "meseta" es intencional, asi que no hay thresholds
  // de latencia. Solo el limite de seguridad de fallas duras.
  thresholds: {
    http_req_failed: [{ threshold: 'rate<0.5', abortOnFail: true, delayAbortEval: '30s' }],
  },
};

export function peticion() {
  const res = http.get(`${TARGET_URL}/`, { timeout: REQ_TIMEOUT });
  check(res, {
    'status 200': (r) => r.status === 200,
    'body empieza con "ok"': (r) => typeof r.body === 'string' && r.body.startsWith('ok'),
  });
}

export function handleSummary(data) {
  return {
    [`${RESULTS_DIR}/summary.json`]: JSON.stringify(data, null, 2),
    stdout: textSummary(data, { indent: ' ', enableColors: true }),
  };
}
