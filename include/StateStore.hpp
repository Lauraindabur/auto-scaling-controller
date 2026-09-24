#pragma once
#include <optional>
#include <string>
#include <vector>
#include "HoltForecaster.hpp"
#include "MetricSnapshot.hpp"
#include "ProactivePolicy.hpp"
#include "SafetyGuards.hpp"

// Estado que sobrevive a un reinicio del controller (seccion 4.10): lo minimo necesario
// para que las politicas y las guardas retomen exactamente donde se quedaron. No incluye
// tsWarmupDesde: es estado efimero del ciclo actual, no de la ultima accion, y perderlo en
// un reinicio solo retrasa el timeout de warm-up, nunca lo salta.
struct EstadoControlador {
    std::optional<DataTs> tsUltimoDatoProcesado;
    std::vector<double> ventanaMaCpu;
    HoltForecaster::Estado holt;
    ProactivePolicy::Estado demanda;
    std::optional<DataTs> tsUltimaSubida;
    std::optional<DataTs> tsUltimaBajada;
};

class StateStore {
public:
    explicit StateStore(std::string ruta) : ruta_(std::move(ruta)) {}

    // Si el archivo no existe o esta corrupto, devuelve un EstadoControlador vacio (todo
    // en su valor por defecto) en vez de lanzar: un reinicio nunca debe impedir que el
    // controller arranque. ultimaCargaValida()/ultimoMotivoCarga() dejan constancia de
    // por que se arranco en frio, para que quien orquesta el ciclo lo registre en el log.
    EstadoControlador cargar();

    // Escritura atomica: se escribe a un archivo temporal en el mismo directorio y se
    // renombra sobre el destino, para que un corte a mitad de escritura (crash, kill -9)
    // nunca deje el archivo de estado a medio escribir.
    void guardar(const EstadoControlador& e) const;

    bool ultimaCargaValida() const { return ultimaCargaValida_; }
    const std::string& ultimoMotivoCarga() const { return ultimoMotivoCarga_; }

private:
    std::string ruta_;
    bool ultimaCargaValida_ = true;
    std::string ultimoMotivoCarga_;
};
