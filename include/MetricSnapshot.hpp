#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace std;

// Tipos que usa toda la cadena de decision fuente -> politicas -> guardas -> combinador
// -> log Asi no se depende del AWS SDK apra pruebes locales
using DataTs = int64_t;

enum class DataQuality { COMPLETE, INCOMPLETE };
enum class Signal { UP, DOWN, HOLD };

enum class Decision { MAINTAIN_CAPACITY, INCREASE_CAPACITY, REDUCE_CAPACITY };
enum class Trigger { NONE, REACTIVE, PROACTIVE, BOTH };

// Estado observado en un periodo completo de CloudWatch, mas el estado del ASG leido
// por API en el mismo instante. optional distingue "la metrica no llego" de "vale 0",
// que es la diferencia entre un dato incompleto y trafico cero (ver DataQuality.hpp).
struct MetricSnapshot {
    DataTs dataTs = 0;
    int periodoSegundos = 60;

    optional<double> cpu;            
    optional<double> requestCount;
    optional<double> healthyHosts;   
    optional<int> desired;          
    optional<int> inService;
    optional<int> pending;

    vector<string> problemas;
};

// HealthyHostCount se trata como un average por minuto es usada por loas guards que cuentan las instancias para decidir si se puede bajar
inline int hostsSanosEnteros(double healthyHosts) {
    if (healthyHosts <= 0.0) return 0;
    return static_cast<int>(healthyHosts);
}

inline const char* nombreDecision(Decision d) {
    switch (d) {
        case Decision::MAINTAIN_CAPACITY: return "MAINTAIN_CAPACITY";
        case Decision::INCREASE_CAPACITY: return "INCREASE_CAPACITY";
        case Decision::REDUCE_CAPACITY:   return "REDUCE_CAPACITY";
    }
    return "MAINTAIN_CAPACITY";
}

inline const char* nombreSenal(Signal s) {
    switch (s) {
        case Signal::UP:   return "UP";
        case Signal::DOWN: return "DOWN";
        case Signal::HOLD: return "HOLD";
    }
    return "HOLD";
}

inline const char* nombreTrigger(Trigger t) {
    switch (t) {
        case Trigger::NONE:      return "NONE";
        case Trigger::REACTIVE:  return "REACTIVE";
        case Trigger::PROACTIVE: return "PROACTIVE";
        case Trigger::BOTH:      return "BOTH";
    }
    return "NONE";
}

inline const char* nombreCalidad(DataQuality q) {
    return q == DataQuality::COMPLETE ? "COMPLETE" : "INCOMPLETE";
}
