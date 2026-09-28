#pragma once
#include <string>
#include <cstdlib> // para poder leer una variable de entorno
#include <stdexcept> //trae runtime_error como tipo de error que se lanza

using namespace std;

struct Config {
    string asgName;
    string region;
    string loadBalancerDim;
    string targetGroupDim;

    int pollIntervalSeg;    // cada cuanto se consulta CloudWatch
    int periodoSeg;         // periodo de las metricas; se decide una vez por periodo
    int maxDataAgeSeg;      // mas viejo que esto -> dato incompleto -> sigue en MAINTAIN

    double umbralAlto;
    double umbralBajo;
    double margenBajada;    // holgura del chequeo n-1 sobre la CPU proyectada
    size_t maVentana;       // muestras de la media movil (solo filtro de ruido)

    double holtAlpha;
    double holtBeta;
    int    horizontePeriodos;   // h: cuantos periodos adelante se pronostica
    double cRpm;                // capacidad de una instancia, en peticiones por minuto

    int cooldownSubidaSeg;
    int cooldownBajadaSeg;
    int warmupTimeoutSeg;   // si una instancia tarda mas que esto en arrancar, se asume atascada y deja de bloquear la subida
    int minCapacity;
    int maxCapacity;

    string stateFile;
    string logFile;
};

//inline -> ayuda a definir una funcion dentro de un .hpp que varios archivos le hacen el include
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

    //---------------------------------------------------------
    // Si la variable no esta definida se usa el valor por defecto; si esta definida con basura, es error.
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

    // Si la condicion no se cumple, lanza el error con el mensaje dado.
    inline void exigir(bool condicion, const string& mensaje) {
        if (!condicion) throw runtime_error("Configuración inválida: " + mensaje);
    }

} 

// Rechaza combinaciones incoherentes: si algo no cumple, lanza runtime_error
// (main.cpp lo captura, imprime el mensaje y termina).
inline void validarConfig(const Config& cfg) {
    using detail::exigir;

    exigir(cfg.umbralBajo < cfg.umbralAlto && cfg.umbralAlto <= 100,
           "se requiere UMBRAL_BAJO < UMBRAL_ALTO <= 100");
    // Al bajar de 2 instancias a 1 la CPU se duplica: no debe cruzar UMBRAL_ALTO y volver a subir.
    exigir(2 * cfg.umbralBajo < cfg.umbralAlto, "se requiere 2 x UMBRAL_BAJO < UMBRAL_ALTO");

    exigir(cfg.holtAlpha > 0.0 && cfg.holtAlpha <= 1.0, "se requiere 0 < HOLT_ALPHA <= 1");
    exigir(cfg.holtBeta > 0.0 && cfg.holtBeta <= 1.0, "se requiere 0 < HOLT_BETA <= 1");
    exigir(cfg.horizontePeriodos >= 1, "HORIZONTE_PERIODOS debe ser >= 1");
    exigir(cfg.cRpm > 0.0, "C_RPM debe ser > 0 (es el divisor del pronóstico)");

    exigir(cfg.minCapacity >= 1 && cfg.minCapacity <= cfg.maxCapacity,
           "se requiere 1 <= MIN_CAPACITY <= MAX_CAPACITY");
    exigir(cfg.cooldownSubidaSeg >= 0 && cfg.cooldownBajadaSeg >= 0, "los cooldowns no pueden ser negativos");
    exigir(cfg.warmupTimeoutSeg >= 1, "WARMUP_TIMEOUT_SEG debe ser >= 1");

    exigir(cfg.pollIntervalSeg >= 1, "POLL_INTERVAL_SEG debe ser >= 1");
    exigir(cfg.periodoSeg >= 1, "PERIOD_SEG debe ser >= 1");

    // Despues de bajar, la MA necesita MA_VENTANA periodos para dejar de ver la CPU de antes,
    // mas 1 periodo por el retraso de CloudWatch (con 3 y 60 s: 240 s).
    const int minimoCooldownBajada = (static_cast<int>(cfg.maVentana) + 1) * cfg.periodoSeg;
    exigir(cfg.cooldownBajadaSeg >= minimoCooldownBajada,
           "se requiere COOLDOWN_BAJADA_SEG >= (MA_VENTANA + 1) x PERIOD_SEG = "
           + to_string(minimoCooldownBajada) + " s");
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

    // Se valida aqui (no en validarConfig) porque un negativo pasado a size_t se volveria un numero enorme.
    int ventana = detail::leerEntero("MA_VENTANA", 3);
    detail::exigir(ventana >= 1, "MA_VENTANA debe ser >= 1");
    cfg.maVentana = static_cast<size_t>(ventana);

    cfg.holtAlpha         = detail::leerDouble("HOLT_ALPHA", 0.5);
    cfg.holtBeta          = detail::leerDouble("HOLT_BETA", 0.3);
    cfg.horizontePeriodos = detail::leerEntero("HORIZONTE_PERIODOS", 3);
    cfg.cRpm              = detail::leerDouble("C_RPM", 480.0);

    cfg.cooldownSubidaSeg = detail::leerEntero("COOLDOWN_SUBIDA_SEG", 120);
    cfg.cooldownBajadaSeg = detail::leerEntero("COOLDOWN_BAJADA_SEG", 240);
    cfg.warmupTimeoutSeg  = detail::leerEntero("WARMUP_TIMEOUT_SEG", 600);
    cfg.minCapacity       = detail::leerEntero("MIN_CAPACITY", 1);
    cfg.maxCapacity       = detail::leerEntero("MAX_CAPACITY", 5);

    cfg.stateFile = detail::leerTexto("STATE_FILE", "state/controller_state.json");
    cfg.logFile   = detail::leerTexto("LOG_FILE", "logs/decisions.jsonl");

    validarConfig(cfg);
    return cfg;
}
