#pragma once
#include <string>
#include <cstdlib> // para poder leer una variable de entorno
#include <stdexcept> //trae runtime_error como tipo de error que se lanza

using namespace std;

struct Config {
    // --- Identidad de la infraestructura (obligatorias, sin valor por defecto) ---
    string asgName;
    string region;
    // Dimensiones de CloudWatch tal como las publica el ALB, ya listas para usar:
    // app/<nombre>/<id> y targetgroup/<nombre>/<id>. Se configuran directamente en vez
    // de derivarlas del ARN, que era fragil y obligaba a validar el formato del ARN.
    string loadBalancerDim;
    string targetGroupDim;

    // --- Ciclo de control ---
    int pollIntervalSeg;    // cada cuanto se consulta CloudWatch
    int periodoSeg;         // periodo de las metricas; se decide una vez por periodo
    int maxDataAgeSeg;      // mas viejo que esto -> dato incompleto -> MAINTAIN

    // --- Reactivo: umbrales estaticos sobre la CPU suavizada ---
    double umbralAlto;
    double umbralBajo;
    double margenBajada;    // holgura del chequeo n-1 sobre la CPU proyectada
    size_t maVentana;       // muestras de la media movil (solo filtro de ruido)

    // --- Proactivo: Holt sobre RequestCount ---
    double holtAlpha;
    double holtBeta;
    int    horizontePeriodos;   // h: cuantos periodos adelante se pronostica
    double cRpm;                // capacidad de una instancia, en peticiones por minuto

    // --- Guardas de seguridad ---
    int cooldownSubidaSeg;
    int cooldownBajadaSeg;
    // Si "hay capacidad en camino" (Pending > 0 o HealthyHostCount < InService) dura mas
    // que esto, se asume una instancia atascada: se registra un WARN y la guarda deja de
    // bloquear la subida, para no quedar sin poder escalar indefinidamente.
    int warmupTimeoutSeg;
    int minCapacity;
    int maxCapacity;

    // --- Rutas de salida ---
    string stateFile;
    string logFile;
};

//inline me ayuda a definir una funcion dentro de un .hpp que varios archivos le hacen el include
namespace detail {

    // tomamos el valor de la variable de entorno como un puntero al texto, y la retornamos como string
    inline string leerEnvObligatoria(const char* nombre) {
        const char* valor = getenv(nombre);
        if (valor == nullptr) {
            throw runtime_error(
                string("Variable de entorno requerida no definida: ") + nombre);
        }
        if (string(valor).empty()) {
            throw runtime_error(string("Variable de entorno vacía: ") + nombre);
        }
        return string(valor);
    }

    inline double aDouble(const char* nombre, const string& texto) {
        try {
            size_t usados = 0;
            double valor = stod(texto, &usados);
            if (usados == texto.size()) return valor;
        } catch (const exception&) {}
        throw runtime_error(string(nombre) + " debe ser un número, valor recibido: '" + texto + "'");
    }

    inline int aEntero(const char* nombre, const string& texto) {
        try {
            size_t usados = 0;
            int valor = stoi(texto, &usados);
            if (usados == texto.size()) return valor;
        } catch (const exception&) {}
        throw runtime_error(string(nombre) + " debe ser un número entero, valor recibido: '" + texto + "'");
    }

    inline double leerDouble(const char* nombre) {
        return aDouble(nombre, leerEnvObligatoria(nombre));
    }

    inline int leerEntero(const char* nombre) {
        return aEntero(nombre, leerEnvObligatoria(nombre));
    }

    // Variantes con valor por defecto: si la variable no esta definida se usa el
    // default, pero si esta definida con basura sigue siendo un error (no se ignora
    // en silencio un valor que el usuario si escribio).
    inline string leerTexto(const char* nombre, const string& porDefecto) {
        const char* valor = getenv(nombre);
        if (valor == nullptr || string(valor).empty()) return porDefecto;
        return string(valor);
    }

    inline double leerDouble(const char* nombre, double porDefecto) {
        const char* valor = getenv(nombre);
        if (valor == nullptr || string(valor).empty()) return porDefecto;
        return aDouble(nombre, string(valor));
    }

    inline int leerEntero(const char* nombre, int porDefecto) {
        const char* valor = getenv(nombre);
        if (valor == nullptr || string(valor).empty()) return porDefecto;
        return aEntero(nombre, string(valor));
    }

} // namespace detail

// Rechaza combinaciones incoherentes: si algo no cumple, lanza runtime_error
// (main.cpp lo captura, imprime el mensaje y termina).
inline void validarConfig(const Config& cfg) {
    if (!(cfg.umbralBajo >= 0 && cfg.umbralBajo < cfg.umbralAlto && cfg.umbralAlto <= 100)) {
        throw runtime_error("Configuración inválida: se requiere 0 <= UMBRAL_BAJO < UMBRAL_ALTO <= 100 (recibido "
                            + to_string(cfg.umbralBajo) + " y " + to_string(cfg.umbralAlto) + ")");
    }

    // Invariante del peor caso al pasar de 2 instancias a 1: si la CPU esta justo por
    // debajo de UMBRAL_BAJO y se retira la mitad de la capacidad, la CPU resultante
    // (el doble) no debe cruzar UMBRAL_ALTO. Sin esto, el controller puede bajar y
    // quedar inmediatamente en zona de subida, oscilando.
    //
    // Nota para la sustentacion: con los valores calibrados (30/70, MARGEN_BAJADA=10)
    // esta invariante implica la mitad de CPU del chequeo n-1 de SafetyGuards, porque
    // n/(n-1) <= 2 para todo n >= 2. Es decir, con 30/70 esa mitad nunca bloquea: la
    // que actua es la del pronostico. Con otra configuracion igualmente valida (p. ej.
    // 33/70: 66 < 70 pasa la invariante, pero 33*2 = 66 > 60) si bloquea. Se conserva
    // como defensa en profundidad para no depender de los umbrales elegidos.
    if (!(2 * cfg.umbralBajo < cfg.umbralAlto)) {
        throw runtime_error("Configuración inválida: se requiere 2 x UMBRAL_BAJO < UMBRAL_ALTO para que "
                            "bajar de 2 a 1 instancia no deje la CPU por encima de UMBRAL_ALTO (recibido "
                            + to_string(cfg.umbralBajo) + " y " + to_string(cfg.umbralAlto) + ")");
    }
    if (!(cfg.margenBajada >= 0 && cfg.margenBajada < cfg.umbralAlto)) {
        throw runtime_error("Configuración inválida: se requiere 0 <= MARGEN_BAJADA < UMBRAL_ALTO (recibido "
                            + to_string(cfg.margenBajada) + ")");
    }
    if (cfg.maVentana < 1) {
        throw runtime_error("Configuración inválida: MA_VENTANA debe ser >= 1");
    }

    if (!(cfg.holtAlpha > 0.0 && cfg.holtAlpha <= 1.0)) {
        throw runtime_error("Configuración inválida: se requiere 0 < HOLT_ALPHA <= 1 (recibido "
                            + to_string(cfg.holtAlpha) + ")");
    }
    if (!(cfg.holtBeta > 0.0 && cfg.holtBeta <= 1.0)) {
        throw runtime_error("Configuración inválida: se requiere 0 < HOLT_BETA <= 1 (recibido "
                            + to_string(cfg.holtBeta) + ")");
    }
    if (cfg.horizontePeriodos < 1) {
        throw runtime_error("Configuración inválida: HORIZONTE_PERIODOS debe ser >= 1");
    }
    if (!(cfg.cRpm > 0.0)) {
        throw runtime_error("Configuración inválida: C_RPM debe ser > 0 (es el divisor del pronóstico, recibido "
                            + to_string(cfg.cRpm) + ")");
    }

    if (cfg.minCapacity < 1 || cfg.minCapacity > cfg.maxCapacity) {
        throw runtime_error("Configuración inválida: se requiere 1 <= MIN_CAPACITY <= MAX_CAPACITY (recibido "
                            + to_string(cfg.minCapacity) + " y " + to_string(cfg.maxCapacity) + ")");
    }
    if (cfg.cooldownSubidaSeg < 0 || cfg.cooldownBajadaSeg < 0) {
        throw runtime_error("Configuración inválida: los cooldowns no pueden ser negativos");
    }
    if (cfg.warmupTimeoutSeg < 1) {
        throw runtime_error("Configuración inválida: WARMUP_TIMEOUT_SEG debe ser >= 1");
    }

    if (cfg.pollIntervalSeg < 1) {
        throw runtime_error("Configuración inválida: POLL_INTERVAL_SEG debe ser >= 1");
    }
    if (cfg.periodoSeg < 1) {
        throw runtime_error("Configuración inválida: PERIOD_SEG debe ser >= 1");
    }
    // Si la edad maxima fuera menor que el periodo, el dato mas reciente posible ya
    // estaria vencido al nacer y el controller decidiria MAINTAIN para siempre.
    if (cfg.maxDataAgeSeg < cfg.periodoSeg) {
        throw runtime_error("Configuración inválida: MAX_DATA_AGE_SEG debe ser >= PERIOD_SEG, si no todo dato "
                            "nace vencido (recibido " + to_string(cfg.maxDataAgeSeg) + " y "
                            + to_string(cfg.periodoSeg) + ")");
    }

    // COOLDOWN_BAJADA_SEG debe cubrir el tiempo que tarda la señal reactiva en reflejar
    // el efecto de la ultima bajada: 3 periodos para que la MA deje de arrastrar CPU
    // previa a la reduccion, mas 1 periodo de margen por el retraso de publicacion de
    // CloudWatch. Sin este piso, el controller podria bajar de nuevo con una MA que
    // todavia esta midiendo la capacidad de ANTES del ultimo recorte.
    const int minimoCooldownBajada = (static_cast<int>(cfg.maVentana) + 1) * cfg.periodoSeg;
    if (cfg.cooldownBajadaSeg < minimoCooldownBajada) {
        throw runtime_error("Configuración inválida: se requiere COOLDOWN_BAJADA_SEG >= (MA_VENTANA + 1) x "
                            "PERIOD_SEG = " + to_string(minimoCooldownBajada) + " s, para que la MA de CPU "
                            "no siga reflejando la capacidad de antes de la ultima bajada (recibido "
                            + to_string(cfg.cooldownBajadaSeg) + " s)");
    }
}

// funcion principal para usar en main, devuelve un config completo cfg ya validado
inline Config cargarConfigDesdeEntorno() {
    Config cfg;
    cfg.asgName         = detail::leerEnvObligatoria("ASG_NAME");
    cfg.region          = detail::leerEnvObligatoria("REGION");
    cfg.loadBalancerDim = detail::leerEnvObligatoria("LOAD_BALANCER_DIM");
    cfg.targetGroupDim  = detail::leerEnvObligatoria("TARGET_GROUP_DIM");

    cfg.pollIntervalSeg = detail::leerEntero("POLL_INTERVAL_SEG", 30);
    cfg.periodoSeg      = detail::leerEntero("PERIOD_SEG", 60);
    cfg.maxDataAgeSeg   = detail::leerEntero("MAX_DATA_AGE_SEG", 180);

    cfg.umbralAlto   = detail::leerDouble("UMBRAL_ALTO", 70.0);
    cfg.umbralBajo   = detail::leerDouble("UMBRAL_BAJO", 30.0);
    cfg.margenBajada = detail::leerDouble("MARGEN_BAJADA", 10.0);

    int ventana = detail::leerEntero("MA_VENTANA", 3);
    if (ventana < 1) {
        throw runtime_error("Configuración inválida: MA_VENTANA debe ser >= 1 (recibido " + to_string(ventana) + ")");
    }
    cfg.maVentana = static_cast<size_t>(ventana);

    cfg.holtAlpha         = detail::leerDouble("HOLT_ALPHA", 0.5);
    cfg.holtBeta          = detail::leerDouble("HOLT_BETA", 0.3);
    cfg.horizontePeriodos = detail::leerEntero("HORIZONTE_PERIODOS", 2);
    cfg.cRpm              = detail::leerDouble("C_RPM", 480.0);

    cfg.cooldownSubidaSeg = detail::leerEntero("COOLDOWN_SUBIDA_SEG", 120);
    // 240 s = 3 periodos de la MA + 1 de margen por el retraso de CloudWatch (con
    // MA_VENTANA=3 y PERIOD_SEG=60). Ver la invariante en validarConfig().
    cfg.cooldownBajadaSeg = detail::leerEntero("COOLDOWN_BAJADA_SEG", 240);
    cfg.warmupTimeoutSeg  = detail::leerEntero("WARMUP_TIMEOUT_SEG", 600);
    cfg.minCapacity       = detail::leerEntero("MIN_CAPACITY", 1);
    cfg.maxCapacity       = detail::leerEntero("MAX_CAPACITY", 5);

    cfg.stateFile = detail::leerTexto("STATE_FILE", "state/controller_state.json");
    cfg.logFile   = detail::leerTexto("LOG_FILE", "logs/decisions.jsonl");

    validarConfig(cfg);
    return cfg;
}
