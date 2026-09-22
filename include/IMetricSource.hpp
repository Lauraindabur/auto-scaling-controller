#pragma once

// Interfaz mínima (que implementa MetricSOurce al hablar con cloudwatch) que DecisionEngine necesita de la fuente de métricas.
// Sin dependencias del AWS SDK, para poder sustituirla por un fake en pruebas.
class IMetricSource {
public:
    // Lectura de las dos métricas de decisión. exito es true solo si AMBAS se obtuvieron;
    // cpuOk/rtOk indican cuál falló. Si el ALB no tiene tráfico, la fuente devuelve responseTime = 0.0 con rtOk = true (no es un fallo).
    // Usarlo como IMetricSource::Lectura a ser un struct dentro de la clase
    struct Lectura {
        bool exito;
        bool cpuOk;
        bool rtOk;
        double cpu;            // CPUUtilization (%)
        double responseTime;   // TargetResponseTime (segundos)
    };

    // Solo observabilidad, con la metrica que solo se anota en el log, no juega un papel en la decision del engine.
    struct MetricasSecundarias {
        bool exito;
        double requestCountPerTarget;
    };

    //usamos el metodo virtual apra que la calse que herede deba escribirlos ->  B : A
    virtual ~IMetricSource() = default;

    virtual Lectura obtenerActual() = 0;
    virtual MetricasSecundarias obtenerMetricasSecundarias() = 0;
};
