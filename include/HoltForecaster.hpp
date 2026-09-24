#pragma once
#include <algorithm>
#include <optional>

// Holt (double exponential smoothing): suaviza nivel y tendencia, sin componente
// estacional. Se aplica al RequestCount TOTAL del ALB en peticiones por minuto.
//
// Se usa el total y no RequestCountPerTarget porque el per-target cambia al cambiar el
// numero de instancias: la serie reaccionaria a las decisiones del propio controller y
// el pronostico dejaria de representar la demanda.
class HoltForecaster {
public:
    HoltForecaster(double alpha, double beta) : alpha_(alpha), beta_(beta) {}

    void observar(double x) {
        // 1er dato: no alcanza para estimar una tendencia, solo se guarda.
        if (!primerValor_.has_value()) {
            primerValor_ = x;
            return;
        }
        // 2o dato: arranca el modelo con L = x2 y T = x2 - x1.
        if (!inicializado_) {
            nivel_ = x;
            tendencia_ = x - *primerValor_;
            inicializado_ = true;
            return;
        }
        const double nivelAnterior = nivel_;
        nivel_ = alpha_ * x + (1.0 - alpha_) * (nivelAnterior + tendencia_);
        tendencia_ = beta_ * (nivel_ - nivelAnterior) + (1.0 - beta_) * tendencia_;
    }

    // Mientras sea false el pronostico no significa nada y quien llama debe mantener.
    bool inicializado() const { return inicializado_; }

    double nivel() const { return nivel_; }
    double tendencia() const { return tendencia_; }

    // x̂ = max(0, L + h*T). Una tendencia a la baja pronunciada da un valor negativo,
    // que como demanda no significa nada: se recorta en 0.
    double pronostico(int horizonte) const {
        if (!inicializado_) return 0.0;
        return std::max(0.0, nivel_ + horizonte * tendencia_);
    }

    // --- Persistencia (StateStore) ---
    struct Estado {
        bool inicializado = false;
        double nivel = 0.0;
        double tendencia = 0.0;
        std::optional<double> primerValor;   // presente si se vio 1 dato pero aun no 2
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
    std::optional<double> primerValor_;
};
