#pragma once
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "Config.hpp"
#include "DataQuality.hpp"
#include "MetricSnapshot.hpp"
#include "ProactivePolicy.hpp"
#include "SafetyGuards.hpp"

using namespace std;


struct Veredicto {
    Decision decision = Decision::MAINTAIN_CAPACITY;
    Trigger trigger = Trigger::NONE;
    int desiredObjetivo = 0;
    string justificacion;
    vector<ResultadoGuarda> guardas;
    string bloqueadaPor;   
};

class DecisionCombiner {
public:
    DecisionCombiner(const Config& cfg, const SafetyGuards& guardas)
        : cfg_(cfg), guardas_(guardas) {}

    Veredicto combinar(const MetricSnapshot& s, const Calidad& cal,
                       Signal reactiva, optional<double> maCpu,
                       const ProactivePolicy::Resultado& proactiva,
                       const EstadoGuardas& est) const {
        const bool proactivaUp = proactiva.listo && proactiva.senal == Signal::UP;
        const bool proactivaDown = proactiva.listo && proactiva.senal == Signal::DOWN;
        const int desiredActual = s.desired.value_or(0);

        Veredicto v;
        v.desiredObjetivo = desiredActual;

        //  SUBIR: basta con que una de las dos pida subir (OR) 
        if (reactiva == Signal::UP || proactivaUp) {
            v.trigger = triggerDe(reactiva == Signal::UP, proactivaUp);
            const VeredictoGuardas vg = guardas_.evaluarSubida(s, cal, est);
            v.guardas = vg.evaluadas;
            if (vg.permitido) {
                v.decision = Decision::INCREASE_CAPACITY;
                v.desiredObjetivo = desiredActual + 1;
            } else {
                v.bloqueadaPor = vg.bloqueadaPor;
            }
            v.justificacion = justificarSubida(v, maCpu, proactiva, desiredActual);
            return v;
        }

        //  BAJAR: exige que las dos pidan bajar (AND) 
        if (reactiva == Signal::DOWN && proactivaDown) {
            v.trigger = Trigger::BOTH;
            const VeredictoGuardas vg = guardas_.evaluarBajada(s, cal, est, *maCpu, proactiva.pronosticoRpm);
            v.guardas = vg.evaluadas;
            if (vg.permitido) {
                v.decision = Decision::REDUCE_CAPACITY;
                v.desiredObjetivo = desiredActual - 1;
            } else {
                v.bloqueadaPor = vg.bloqueadaPor;
            }
            v.justificacion = justificarBajada(v, *maCpu, proactiva, desiredActual);
            return v;
        }

        // -> el caso de señales que no se pusieron de acuerdo 
        v.trigger = Trigger::NONE;
        v.justificacion = justificarSinCambio(reactiva, maCpu, proactiva);
        return v;
    }

private:
    static Trigger triggerDe(bool reactivaPide, bool proactivaPide) {
        if (reactivaPide && proactivaPide) return Trigger::BOTH;
        return reactivaPide ? Trigger::REACTIVE : Trigger::PROACTIVE;
    }

    static string dec(double v) {
        ostringstream oss;
        oss << fixed << setprecision(2) << v;
        return oss.str();
    }

    static string detalleGuarda(const vector<ResultadoGuarda>& guardas, const string& nombre) {
        for (const auto& g : guardas) if (g.nombre == nombre) return g.detalle;
        return "";
    }

    string justificarSubida(const Veredicto& v, optional<double> maCpu,
                                 const ProactivePolicy::Resultado& p, int desiredActual) const {
        vector<string> motivos;
        if (v.trigger == Trigger::REACTIVE || v.trigger == Trigger::BOTH) {
            motivos.push_back("MA_CPU " + dec(*maCpu) + " > UMBRAL_ALTO " + dec(cfg_.umbralAlto));
        }
        if (v.trigger == Trigger::PROACTIVE || v.trigger == Trigger::BOTH) {
            motivos.push_back("pronostico " + dec(p.pronosticoRpm) + " RPM exige " + to_string(p.necesarias)
                              + " instancias (actual " + to_string(desiredActual) + ")");
        }
        ostringstream j;
        j << motivos[0];
        for (size_t i = 1; i < motivos.size(); i++) j << " y " << motivos[i];

        if (v.decision == Decision::INCREASE_CAPACITY) {
            j << "; sube a " << v.desiredObjetivo;
        } else {
            j << "; bloqueada por " << v.bloqueadaPor << ": " << detalleGuarda(v.guardas, v.bloqueadaPor);
        }
        return j.str();
    }

    string justificarBajada(const Veredicto& v, double maCpu,
                                 const ProactivePolicy::Resultado& p, int desiredActual) const {
        ostringstream j;
        j << "MA_CPU " << dec(maCpu) << " < UMBRAL_BAJO " << dec(cfg_.umbralBajo)
          << " y pronostico " << dec(p.pronosticoRpm) << " RPM exige solo " << p.necesarias
          << " instancias (actual " << desiredActual << ")";
        if (v.decision == Decision::REDUCE_CAPACITY) {
            j << "; baja a " << v.desiredObjetivo;
        } else {
            j << "; bloqueada por " << v.bloqueadaPor << ": " << detalleGuarda(v.guardas, v.bloqueadaPor);
        }
        return j.str();
    }

    string justificarSinCambio(Signal reactiva, optional<double> maCpu,
                                    const ProactivePolicy::Resultado& p) const {
        ostringstream j;
        j << "sin señal de cambio: reactivo " << nombreSenal(reactiva);
        if (maCpu.has_value()) j << " (MA_CPU " << dec(*maCpu) << ")";
        j << "; proactivo ";
        if (!p.listo) {
            j << "sin modelo todavia";
        } else {
            j << nombreSenal(p.senal) << " (pronostico " << dec(p.pronosticoRpm) << " RPM, necesarias "
              << p.necesarias << ")";
            if (p.guardaPicoAplicada) j << ", guarda de picos aplicada";
        }
        return j.str();
    }

    const Config& cfg_;
    const SafetyGuards& guardas_;
};
