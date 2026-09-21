#pragma once
#include <string>
#include <cstdlib>
#include <stdexcept>

using namespace std;

struct Config {
    string asgName;
    string region;
    string targetGroupArn;
    string loadBalancerArn;
    double umbralSubida;
    double umbralBajada;
    double umbralRtSubida;   // segundos
    double umbralRtBajada;   // segundos
    size_t ventanaMA;
    int cooldownCiclos;
    int timeoutOperacionCiclos;
    int capacidadMin;
    int capacidadMax;
    int pasoMaximoSubida;
    int intervaloCicloSegundos;
};

namespace detail {

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

    // Convierte el texto completo; "abc" o "70x" dan un error que dice cuál variable está mal.
    inline double leerDouble(const char* nombre) {
        string texto = leerEnvObligatoria(nombre);
        try {
            size_t usados = 0;
            double valor = stod(texto, &usados);
            if (usados == texto.size()) return valor;
        } catch (const exception&) {}
        throw runtime_error(string(nombre) + " debe ser un número, valor recibido: '" + texto + "'");
    }

    inline int leerEntero(const char* nombre) {
        string texto = leerEnvObligatoria(nombre);
        try {
            size_t usados = 0;
            int valor = stoi(texto, &usados);
            if (usados == texto.size()) return valor;
        } catch (const exception&) {}
        throw runtime_error(string(nombre) + " debe ser un número entero, valor recibido: '" + texto + "'");
    }

} // namespace detail

// Rechaza combinaciones incoherentes: si algo no cumple, lanza runtime_error
// (main.cpp lo captura, imprime el mensaje y termina).
inline void validarConfig(const Config& cfg) {
    if (!(cfg.umbralBajada >= 0 && cfg.umbralBajada < cfg.umbralSubida && cfg.umbralSubida <= 100)) {
        throw runtime_error("Configuración inválida: se requiere 0 <= UMBRAL_BAJADA < UMBRAL_SUBIDA <= 100 (recibido "
                            + to_string(cfg.umbralBajada) + " y " + to_string(cfg.umbralSubida) + ")");
    }
    if (!(cfg.umbralRtBajada > 0 && cfg.umbralRtBajada <= cfg.umbralRtSubida)) {
        throw runtime_error("Configuración inválida: se requiere 0 < UMBRAL_RT_BAJADA <= UMBRAL_RT_SUBIDA (recibido "
                            + to_string(cfg.umbralRtBajada) + " y " + to_string(cfg.umbralRtSubida) + ")");
    }
    if (cfg.capacidadMin < 1 || cfg.capacidadMin > cfg.capacidadMax) {
        throw runtime_error("Configuración inválida: se requiere 1 <= CAPACIDAD_MIN <= CAPACIDAD_MAX (recibido "
                            + to_string(cfg.capacidadMin) + " y " + to_string(cfg.capacidadMax) + ")");
    }
    if (cfg.pasoMaximoSubida < 1) {
        throw runtime_error("Configuración inválida: PASO_MAXIMO_SUBIDA debe ser >= 1");
    }
    if (cfg.cooldownCiclos < 0) {
        throw runtime_error("Configuración inválida: COOLDOWN_CICLOS no puede ser negativo");
    }
    if (cfg.timeoutOperacionCiclos < 1) {
        throw runtime_error("Configuración inválida: TIMEOUT_OPERACION_CICLOS debe ser >= 1");
    }
    if (cfg.intervaloCicloSegundos < 1) {
        throw runtime_error("Configuración inválida: INTERVALO_CICLO_SEGUNDOS debe ser >= 1");
    }
}

inline Config cargarConfigDesdeEntorno() {
    Config cfg;
    cfg.asgName         = detail::leerEnvObligatoria("ASG_NAME");
    cfg.region          = detail::leerEnvObligatoria("AWS_REGION");
    cfg.targetGroupArn  = detail::leerEnvObligatoria("TARGET_GROUP_ARN");
    cfg.loadBalancerArn = detail::leerEnvObligatoria("LOAD_BALANCER_ARN");

    cfg.umbralSubida = detail::leerDouble("UMBRAL_SUBIDA");
    cfg.umbralBajada = detail::leerDouble("UMBRAL_BAJADA");
    cfg.umbralRtSubida = detail::leerDouble("UMBRAL_RT_SUBIDA");
    cfg.umbralRtBajada = detail::leerDouble("UMBRAL_RT_BAJADA");

    int ventana = detail::leerEntero("VENTANA_MA");
    if (ventana < 1) {
        throw runtime_error("Configuración inválida: VENTANA_MA debe ser >= 1 (recibido " + to_string(ventana) + ")");
    }
    cfg.ventanaMA = static_cast<size_t>(ventana);

    cfg.cooldownCiclos = detail::leerEntero("COOLDOWN_CICLOS");
    cfg.timeoutOperacionCiclos = detail::leerEntero("TIMEOUT_OPERACION_CICLOS");
    cfg.capacidadMin   = detail::leerEntero("CAPACIDAD_MIN");
    cfg.capacidadMax   = detail::leerEntero("CAPACIDAD_MAX");
    cfg.pasoMaximoSubida = detail::leerEntero("PASO_MAXIMO_SUBIDA");
    cfg.intervaloCicloSegundos = detail::leerEntero("INTERVALO_CICLO_SEGUNDOS");

    validarConfig(cfg);
    return cfg;
}
