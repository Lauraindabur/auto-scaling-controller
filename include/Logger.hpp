#pragma once
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include "MetricSnapshot.hpp"
#include "SafetyGuards.hpp"   // ResultadoGuarda

// Una linea del log de decisiones (seccion 4.11): todo lo necesario para reconstruir,
// desde el JSONL, hora, metricas e intervalo considerados, capacidad y su estado,
// decision y justificacion, accion pedida y resultado. Los campos opcionales salen como
// null en el JSON cuando no aplican.
struct RegistroCiclo {
    // dataTs es obligatorio: quien orquesta el ciclo (Paso 7/8) solo llama a registrar()
    // cuando hay un periodo de CloudWatch que procesar, aunque el dato en si sea incompleto.
    DataTs dataTs = 0;
    int periodoSegundos = 60;
    int ventanaMa = 0;              // cuantos periodos usa la MA reactiva (MA_VENTANA)

    // --- Metricas crudas, tal como llegaron (sin la interpretacion de trafico 0) ---
    std::optional<double> cpu;
    std::optional<double> requestCount;
    std::optional<double> healthyHosts;

    DataQuality calidad = DataQuality::INCOMPLETE;
    std::vector<std::string> dataIssues;

    // --- Reactivo ---
    std::optional<double> maCpu;
    Signal reactiveSignal = Signal::HOLD;

    // --- Proactivo. forecastRpm/neededInstances quedan en null mientras Holt no este
    // inicializado: un 0 ahi se leeria como "no hace falta ninguna instancia", que es
    // una afirmacion distinta de "todavia no hay pronostico". ---
    std::optional<double> holtLevel;
    std::optional<double> holtTrend;
    std::optional<double> forecastRpm;
    int horizon = 0;
    std::optional<int> neededInstances;
    Signal proactiveSignal = Signal::HOLD;
    bool spikeGuardApplied = false;

    // --- Capacidad (DescribeAutoScalingGroups) ---
    std::optional<int> desired;
    std::optional<int> inService;
    std::optional<int> pending;

    // --- Guardas: todas las evaluadas, no solo la que bloqueo ---
    std::vector<ResultadoGuarda> guards;
    std::string blockedBy;   // "" si no fue bloqueada por ninguna guarda

    Decision decision = Decision::MAINTAIN_CAPACITY;
    Trigger trigger = Trigger::NONE;
    std::string justification;

    // --- Accion sobre el ASG ---
    std::string actionRequested = "none";     // p. ej. "SetDesiredCapacity 2->3"
    std::string actionResult = "SKIPPED";     // "OK" | "ERROR: ..." | "SKIPPED"
};

class Logger {
public:
    explicit Logger(const std::string& rutaArchivo);
    ~Logger();

    void registrar(const RegistroCiclo& r);

private:
    std::string timestampActual() const;
    std::string escapar(const std::string& texto) const;
    void escribirLinea(const std::string& json);

    std::ofstream archivo_;
    long cycleId_ = 0;
};
