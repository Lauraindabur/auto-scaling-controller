#pragma once

// Interfaz mínima que DecisionEngine necesita de la fuente de métricas.
// Sin dependencias del AWS SDK, para poder sustituirla por un fake en pruebas.
class IMetricSource {
public:
    // Lectura de las dos métricas de decisión. exito es true solo si AMBAS se obtuvieron;
    // cpuOk/rtOk indican cuál falló. Si el ALB no tiene tráfico, la fuente devuelve
    // responseTime = 0.0 con rtOk = true (no es un fallo).
    struct Lectura {
        bool exito;
        bool cpuOk;
        bool rtOk;
        double cpu;            // CPUUtilization (%)
        double responseTime;   // TargetResponseTime (segundos)
    };

    // Solo observabilidad: no participa en la decisión.
    struct MetricasSecundarias {
        bool exito;
        double requestCountPerTarget;
    };

    virtual ~IMetricSource() = default;

    virtual Lectura obtenerActual() = 0;
    virtual MetricasSecundarias obtenerMetricasSecundarias() = 0;
};
