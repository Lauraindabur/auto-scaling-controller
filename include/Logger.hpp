#pragma once
#include <string>
#include <fstream>
#include <optional>

// Una linea del log de decisiones. Los campos opcionales salen como null en el JSON
// cuando no aplican (p. ej. metrica no disponible).
struct RegistroCiclo {
    std::optional<double> cpuUtilization;
    std::optional<double> movingAverageCpu;
    std::optional<double> targetResponseTime;
    std::optional<double> movingAverageResponseTime;
    std::optional<int> capacidadActual;
    std::string decision;
    std::string decisionTrigger = "NONE";   // CPU | RT | CPU+RT | NONE
    std::string justificacion;
    std::string accionSolicitada = "NONE";
    std::string resultadoAccion = "N/A";
    std::string estadoOperacion = "NONE";
    int cooldownRestante = 0;
    std::optional<double> requestCountPerTarget;
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
