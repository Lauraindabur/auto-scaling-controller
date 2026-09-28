#pragma once
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include "MetricSnapshot.hpp"
#include "SafetyGuards.hpp"  
using namespace std;

struct RegistroCiclo {
    DataTs dataTs = 0;  //dataTs es el timestamp del minuto que se esta procesando -> NO el timestamp de cuando se procesa
    int periodoSegundos = 60;
    int ventanaMa = 0;              

    optional<double> cpu;
    optional<double> requestCount;
    optional<double> healthyHosts;

    DataQuality calidad = DataQuality::INCOMPLETE;
    vector<string> dataIssues;

    optional<double> maCpu;  //para el reactivo
    Signal reactiveSignal = Signal::HOLD;

    optional<double> holtLevel;  //para el proactivo
    optional<double> holtTrend;
    optional<double> forecastRpm;
    int horizon = 0;
    optional<int> neededInstances;
    Signal proactiveSignal = Signal::HOLD;
    bool spikeGuardApplied = false;

    optional<int> desired;
    optional<int> inService;
    optional<int> pending;

    vector<ResultadoGuarda> guards;
    string blockedBy;  

    Decision decision = Decision::MAINTAIN_CAPACITY;
    Trigger trigger = Trigger::NONE;
    string justification;

    // Se muestra la accion sobre el ASG 
    string actionRequested = "none";     // -> por ejemplo SetDesiredCapacity 2->3
    string actionResult = "SKIPPED";     //  es o OK o error o skipped
};

class Logger {
public:
    explicit Logger(const string& rutaArchivo);
    ~Logger();

    void registrar(const RegistroCiclo& r);

private:
    string timestampActual() const;
    string escapar(const string& texto) const;
    void escribirLinea(const string& json);

    ofstream archivo_;
    long cycleId_ = 0;
};
