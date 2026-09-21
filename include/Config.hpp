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
        return string(valor);
    }

} // namespace detail

inline Config cargarConfigDesdeEntorno() {
    Config cfg;
    cfg.asgName         = detail::leerEnvObligatoria("ASG_NAME");
    cfg.region          = detail::leerEnvObligatoria("AWS_REGION");
    cfg.targetGroupArn  = detail::leerEnvObligatoria("TARGET_GROUP_ARN");
    cfg.loadBalancerArn = detail::leerEnvObligatoria("LOAD_BALANCER_ARN");

    cfg.umbralSubida = stod(detail::leerEnvObligatoria("UMBRAL_SUBIDA"));
    cfg.umbralBajada = stod(detail::leerEnvObligatoria("UMBRAL_BAJADA"));
    cfg.umbralRtSubida = stod(detail::leerEnvObligatoria("UMBRAL_RT_SUBIDA"));
    cfg.umbralRtBajada = stod(detail::leerEnvObligatoria("UMBRAL_RT_BAJADA"));

    cfg.ventanaMA = static_cast<size_t>(
        stoul(detail::leerEnvObligatoria("VENTANA_MA")));

    cfg.cooldownCiclos = stoi(detail::leerEnvObligatoria("COOLDOWN_CICLOS"));
    cfg.timeoutOperacionCiclos = stoi(detail::leerEnvObligatoria("TIMEOUT_OPERACION_CICLOS"));
    cfg.capacidadMin   = stoi(detail::leerEnvObligatoria("CAPACIDAD_MIN"));
    cfg.capacidadMax   = stoi(detail::leerEnvObligatoria("CAPACIDAD_MAX"));
    cfg.pasoMaximoSubida = stoi(detail::leerEnvObligatoria("PASO_MAXIMO_SUBIDA"));

    cfg.intervaloCicloSegundos =
        stoi(detail::leerEnvObligatoria("INTERVALO_CICLO_SEGUNDOS"));

    return cfg;
}