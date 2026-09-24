#pragma once
#include <optional>
#include <vector>
#include "MetricSnapshot.hpp"   // Signal
#include "MovingAverage.hpp"

// Parte reactiva de la politica (static thresholds en la taxonomia de Al-Dhuraibi et al.):
// compara la CPU promedio del ASG contra dos umbrales fijos.
//
// La media movil esta aqui SOLO como filtro de ruido, no como pronostico: un pico aislado
// de un minuto no debe mover la capacidad. Quien anticipa la demanda es ProactivePolicy.
class ReactivePolicy {
public:
    ReactivePolicy(double umbralAlto, double umbralBajo, size_t ventana)
        : ma_(ventana), umbralAlto_(umbralAlto), umbralBajo_(umbralBajo) {}

    // Se llama una sola vez por dato nuevo y completo. Un ciclo con dato incompleto NO
    // debe llamarla: la ventana no avanza y la señal se queda como estaba, que es
    // justamente lo que hace que un fallo de metricas derive en MAINTAIN.
    void observar(double cpu) { ma_.agregar(cpu); }

    bool listo() const { return ma_.tieneSuficienteHistorial(); }

    // nullopt mientras la ventana no este llena: evita publicar un promedio calculado
    // sobre menos muestras de las configuradas, que seria mas ruidoso de lo previsto.
    std::optional<double> maCpu() const {
        if (!listo()) return std::nullopt;
        return ma_.valor();
    }

    // Umbrales estrictos a proposito: una MA exactamente en el umbral no dispara nada.
    // Con >= o <=, una CPU que se queda pegada al limite produciria decisiones en ciclos
    // consecutivos.
    Signal senal() const {
        if (!listo()) return Signal::HOLD;
        const double valor = ma_.valor();
        if (valor > umbralAlto_) return Signal::UP;
        if (valor < umbralBajo_) return Signal::DOWN;
        return Signal::HOLD;
    }

    // --- Persistencia (StateStore) ---
    std::vector<double> ventana() const { return ma_.valores(); }
    void restaurar(const std::vector<double>& muestras) { ma_.prellenar(muestras); }

private:
    MovingAverage ma_;
    double umbralAlto_;
    double umbralBajo_;
};
