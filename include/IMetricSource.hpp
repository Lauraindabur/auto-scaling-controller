#pragma once

// Interfaz mínima que DecisionEngine necesita de la fuente de métricas.
// Sin dependencias del AWS SDK, para poder sustituirla por un fake en pruebas.
class IMetricSource {
public:
    struct Lectura {
        bool exito;
        double valor;
    };

    struct MetricasSecundarias {
        bool exito;
        double targetResponseTime;
        double requestCountPerTarget;
    };

    virtual ~IMetricSource() = default;

    virtual Lectura obtenerActual() = 0;
    virtual MetricasSecundarias obtenerMetricasSecundarias() = 0;
};
