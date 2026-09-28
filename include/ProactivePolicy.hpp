#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>
#include "HoltForecaster.hpp"
#include "MetricSnapshot.hpp"   

using namespace std;

class ProactivePolicy {
public:
    ProactivePolicy(HoltForecaster& holt, double cRpm, int horizonte,
                    int minCapacidad, int maxCapacidad)
        : holt_(holt), cRpm_(cRpm), horizonte_(horizonte),
          minCapacidad_(minCapacidad), maxCapacidad_(maxCapacidad) {}

    // El trafico 0 se pone como 0.0 no se omite porque omitirlo dejaria a Holt pensndo que la demanda sigue como el ultimo valor que vió 
    void observar(double requestCount) {
        holt_.observar(requestCount);
        historia_.push_back(requestCount);
        if (historia_.size() > 3) historia_.pop_front();
    }

    struct Resultado {
        bool   listo = false;                 // se pone falso para que el holt tenga itmepo de recibir minimo 2 datos y pasa a true
        Signal senal = Signal::HOLD;
        bool   guardaPicoAplicada = false;   
        double pronosticoRpm = 0.0;
        int    necesarias = 0;
    };

    // capacidadActual = InService + Pending.  
    // inservice es el num de instancias que estan listas para recibir trafico. pending es el num de instancias que estan en proceso de crearse y todavia no estan listas para recibir trafico. 
    Resultado evaluar(int capacidadActual) const {
        Resultado r;
        if (!holt_.inicializado()) return r;

        r.listo = true;
        r.pronosticoRpm = holt_.pronostico(horizonte_);
        r.necesarias = necesariasPara(r.pronosticoRpm);

        if (r.necesarias > capacidadActual)      r.senal = Signal::UP;
        else if (r.necesarias < capacidadActual) r.senal = Signal::DOWN;

        // guard para picos, se define que solo se sube  si los ultimos 3 valores de requestCount estan en crecimiento. Si no hay los 3 todavía no se puede confirmar la tendencia y el UP se cambia a HOLD
        if (r.senal == Signal::UP && !demandaCreciendo()) {
            r.senal = Signal::HOLD;
            r.guardaPicoAplicada = true;
        }
        return r;
    }

    struct Estado {
        vector<double> ultimosValores;
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
        const int crudas = static_cast<int>(ceil(pronosticoRpm / cRpm_));
        return clamp(crudas, minCapacidad_, maxCapacidad_);
    }

    // Para el StateStore persiste los 3 datos del MA, asi que un reinicio no daña una  primera subida. 
    bool demandaCreciendo() const {
        if (historia_.size() < 3) return false;
        return historia_[2] > historia_[1] && historia_[1] > historia_[0];
    }

    HoltForecaster& holt_;
    double cRpm_;
    int horizonte_;
    int minCapacidad_;
    int maxCapacidad_;
    deque<double> historia_;   
};
