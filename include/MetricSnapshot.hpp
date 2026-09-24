#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Tipos compartidos por toda la cadena de decision: fuente de metricas -> politicas
// -> guardas -> combinador -> log. No dependen del AWS SDK a proposito, para que los
// tests de secuencia puedan construir snapshots a mano.

// Epoch en segundos del INICIO del periodo de CloudWatch al que pertenece el dato.
// Este es el "ahora" de la logica de decision: los cooldowns y la edad de los datos se
// miden contra este timestamp, no contra el reloj del sistema (que solo aparece en el
// campo wall_time del log y como parametro para detectar datos viejos).
using DataTs = std::int64_t;

enum class DataQuality { COMPLETE, INCOMPLETE };

// Señal que emite cada politica (reactiva y proactiva) por separado.
enum class Signal { UP, DOWN, HOLD };

enum class Decision { MAINTAIN_CAPACITY, INCREASE_CAPACITY, REDUCE_CAPACITY };

// Que politica origino la decision, para el campo trigger del log.
enum class Trigger { NONE, REACTIVE, PROACTIVE, BOTH };

// Estado observado en un periodo completo de CloudWatch, mas el estado del ASG leido
// por API en el mismo instante. optional distingue "la metrica no llego" de "vale 0",
// que es la diferencia entre un dato incompleto y trafico cero (ver DataQuality.hpp).
struct MetricSnapshot {
    DataTs dataTs = 0;
    int periodoSegundos = 60;

    std::optional<double> cpu;            // AWS/EC2 CPUUtilization, Average, por ASG
    std::optional<double> requestCount;   // AWS/ApplicationELB RequestCount, Sum (RPM), por LB
    std::optional<double> healthyHosts;   // AWS/ApplicationELB HealthyHostCount, Average, por TG

    std::optional<int> desired;           // DescribeAutoScalingGroups
    std::optional<int> inService;
    std::optional<int> pending;

    // Problemas que solo la fuente puede detectar (fallo de la llamada, respuesta
    // sin datapoints). evaluarCalidad() les añade los que se deducen del contenido.
    std::vector<std::string> problemas;
};

// HealthyHostCount es un Average por minuto: en las transiciones llega fraccionario
// (p. ej. 1.5 cuando una instancia entro a mitad del periodo). Toda guarda que cuente
// instancias usa este piso, para no asumir capacidad que todavia no esta completa.
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
