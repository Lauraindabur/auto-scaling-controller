#pragma once
#include <optional>
#include <vector>
#include "MetricSnapshot.hpp"   // Signal
#include "MovingAverage.hpp"

using namespace std;


class ReactivePolicy {
public:
    ReactivePolicy(double umbralAlto, double umbralBajo, size_t ventana)
        : ma_(ventana), umbralAlto_(umbralAlto), umbralBajo_(umbralBajo) {}

    // Se llama cada dato nuevo si el ciclo es icompleto no se llama y MA no avanza hasta derivar en MANTAIN
    void observar(double cpu) { ma_.agregar(cpu); }
    bool listo() const { return ma_.tieneSuficienteHistorial(); }

    optional<double> maCpu() const {
        if (!listo()) return nullopt;
        return ma_.valor();
    }

    Signal senal() const {
        if (!listo()) return Signal::HOLD;
        const double valor = ma_.valor();
        if (valor > umbralAlto_) return Signal::UP;
        if (valor < umbralBajo_) return Signal::DOWN;
        return Signal::HOLD;
    }

    vector<double> ventana() const { return ma_.valores(); }
    void restaurar(const vector<double>& muestras) { ma_.prellenar(muestras); }

private:
    MovingAverage ma_;
    double umbralAlto_;
    double umbralBajo_;
};
