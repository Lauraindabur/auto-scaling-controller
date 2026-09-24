#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>
#include "HoltForecaster.hpp"
#include "MetricSnapshot.hpp"   // Signal

// Parte proactiva de la politica (time series analysis en la taxonomia de Al-Dhuraibi
// et al.): traduce el pronostico de demanda a un numero de instancias y lo compara con
// la capacidad actual.
//
// necesarias = clamp(ceil(x̂ / C_RPM), MIN, MAX), donde C_RPM es lo que aguanta una
// instancia al ~70 % de CPU. Anticipa el warm-up: el horizonte h esta calibrado para que
// las instancias esten sanas cuando llegue la demanda pronosticada.
class ProactivePolicy {
public:
    ProactivePolicy(HoltForecaster& holt, double cRpm, int horizonte,
                    int minCapacidad, int maxCapacidad)
        : holt_(holt), cRpm_(cRpm), horizonte_(horizonte),
          minCapacidad_(minCapacidad), maxCapacidad_(maxCapacidad) {}

    // El trafico cero se alimenta como 0.0, no se omite: omitirlo dejaria a Holt creyendo
    // que la demanda sigue en el ultimo valor visto y bloquearia la bajada a MIN.
    void observar(double requestCount) {
        holt_.observar(requestCount);
        historia_.push_back(requestCount);
        if (historia_.size() > 3) historia_.pop_front();
    }

    struct Resultado {
        bool   listo = false;                 // false mientras Holt no tenga 2 datos
        Signal senal = Signal::HOLD;
        bool   guardaPicoAplicada = false;    // un UP se degrado a HOLD
        double pronosticoRpm = 0.0;
        int    necesarias = 0;
    };

    // capacidadActual = InService + Pending. Se cuenta la capacidad que ya viene en
    // camino para no pedir instancias de mas mientras las anteriores todavia arrancan.
    Resultado evaluar(int capacidadActual) const {
        Resultado r;
        if (!holt_.inicializado()) return r;

        r.listo = true;
        r.pronosticoRpm = holt_.pronostico(horizonte_);
        r.necesarias = necesariasPara(r.pronosticoRpm);

        if (r.necesarias > capacidadActual)      r.senal = Signal::UP;
        else if (r.necesarias < capacidadActual) r.senal = Signal::DOWN;

        // Guarda contra picos transitorios: subir solo si la demanda observada viene
        // creciendo. Degrada UP a HOLD y nunca toca DOWN, porque bajar ante un pronostico
        // a la baja es reversible y barato, mientras que subir por un pico de un minuto
        // cuesta una instancia que estara lista justo cuando el pico ya paso.
        if (r.senal == Signal::UP && !demandaCreciendo()) {
            r.senal = Signal::HOLD;
            r.guardaPicoAplicada = true;
        }
        return r;
    }

    // --- Persistencia (StateStore) ---
    struct Estado {
        // La ventana completa de la guarda: hasta 3 valores, del mas viejo al mas nuevo.
        // Se persisten los 3 y no solo los 2 ultimos para que un reinicio produzca las
        // mismas decisiones que si no hubiera ocurrido; con 2 valores, la guarda no podria
        // confirmar los dos crecimientos y se perderia la primera subida proactiva.
        std::vector<double> ultimosValores;
    };

    Estado estado() const {
        Estado e;
        for (double v : historia_) e.ultimosValores.push_back(v);
        return e;
    }

    void restaurar(const Estado& e) {
        historia_.clear();
        for (double v : e.ultimosValores) {
            historia_.push_back(v);
            if (historia_.size() > 3) historia_.pop_front();
        }
    }

private:
    int necesariasPara(double pronosticoRpm) const {
        const int crudas = static_cast<int>(std::ceil(pronosticoRpm / cRpm_));
        return std::clamp(crudas, minCapacidad_, maxCapacidad_);
    }

    // xt > xt-1 > xt-2: hacen falta 3 observaciones. El StateStore persiste las 3, asi que
    // un reinicio no degrada la primera subida. Con menos de 3 (arranque en frio o estado
    // parcial) no se puede confirmar la tendencia y el UP se degrada a HOLD.
    bool demandaCreciendo() const {
        if (historia_.size() < 3) return false;
        return historia_[2] > historia_[1] && historia_[1] > historia_[0];
    }

    HoltForecaster& holt_;
    double cRpm_;
    int horizonte_;
    int minCapacidad_;
    int maxCapacidad_;
    std::deque<double> historia_;   // hasta 3: xt-2, xt-1, xt
};
