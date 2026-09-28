#pragma once
#include <algorithm>
#include <optional>

using namespace std;

// Holt -> suaviza nivel y tendencia, lo aplicamos al RequestCount TOTAL del ALB en peticiones cada 60seg.
class HoltForecaster {
public:
    HoltForecaster(double alpha, double beta) : alpha_(alpha), beta_(beta) {}
    // como el primervalor no nos yauda a estimar tendencia solo se guarda, el 2do arranca el modelo con L = x2 y T = x2 - x1. A partir de ahi se aplica la formula de Holt.
    void observar(double x) {
        if (!primerValor_.has_value()) {
            primerValor_ = x;
            return;
        }
        if (!inicializado_) {  //2do dato
            nivel_ = x;
            tendencia_ = x - *primerValor_;
            inicializado_ = true;
            return;
        }
        const double nivelAnterior = nivel_;
        nivel_ = alpha_ * x + (1.0 - alpha_) * (nivelAnterior + tendencia_);
        tendencia_ = beta_ * (nivel_ - nivelAnterior) + (1.0 - beta_) * tendencia_;
    }

    // Siempre q retorne false el pronostico no significa nada y quien llama debe MANTAIN.
    bool inicializado() const { return inicializado_; }

    double nivel() const { return nivel_; }
    double tendencia() const { return tendencia_; }

    double pronostico(int horizonte) const {
        if (!inicializado_) return 0.0;
        return max(0.0, nivel_ + horizonte * tendencia_);
    }

    // struc para persistencia, usada por statestore se guarda la persistencia del holt
    struct Estado {
        bool inicializado = false;
        double nivel = 0.0;
        double tendencia = 0.0;
        optional<double> primerValor;   
    };

    Estado estado() const { return {inicializado_, nivel_, tendencia_, primerValor_}; }

    void restaurar(const Estado& e) {
        inicializado_ = e.inicializado;
        nivel_ = e.nivel;
        tendencia_ = e.tendencia;
        primerValor_ = e.primerValor;
    }

private:
    double alpha_;
    double beta_;
    bool inicializado_ = false;
    double nivel_ = 0.0;
    double tendencia_ = 0.0;
    optional<double> primerValor_;
};
