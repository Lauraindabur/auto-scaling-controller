#pragma once
#include <optional>
#include <string>
#include <vector>
#include "HoltForecaster.hpp"
#include "MetricSnapshot.hpp"
#include "ProactivePolicy.hpp"
#include "SafetyGuards.hpp"

using namespace std;

// Aca se define el estado que sobrevive a un reinicio del controller 
struct EstadoControlador {
    optional<DataTs> tsUltimoDatoProcesado;
    vector<double> ventanaMaCpu;
    HoltForecaster::Estado holt;
    ProactivePolicy::Estado demanda;
    optional<DataTs> tsUltimaSubida;
    optional<DataTs> tsUltimaBajada;
};

class StateStore {
public:
    explicit StateStore(string ruta) : ruta_(move(ruta)) {}
    EstadoControlador cargar();

    // la idea es que se escribe a un archivo temporal en el mismo directorio y se renombra sobre el destino
    void guardar(const EstadoControlador& e) const;

    bool ultimaCargaValida() const { return ultimaCargaValida_; }
    const string& ultimoMotivoCarga() const { return ultimoMotivoCarga_; }

private:
    string ruta_;
    bool ultimaCargaValida_ = true;
    string ultimoMotivoCarga_;
};
