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

// Reglas de la seccion 4.8: junta la señal reactiva y la proactiva en una sola decision,
// aplica las guardas correspondientes y arma la justificacion legible del log.
//
//   INCREASE_CAPACITY: (reactiva == UP)  O (proactiva == UP), y las guardas de subida pasan.
//   REDUCE_CAPACITY:   (reactiva == DOWN) Y (proactiva == DOWN), y las guardas de bajada pasan.
//   MAINTAIN_CAPACITY: todo lo demas.
//
// "Datos incompletos -> MAINTAIN" no se comprueba aqui aparte: la guarda datos_completos
// (SafetyGuards) ya es la primera de las dos listas y bloquea igual una subida que una
// bajada, asi que basta con no actualizar los buffers de las politicas en un ciclo
// incompleto (responsabilidad de quien orquesta el ciclo, no de este combinador) para que
// la señal that llega aqui sea la del ultimo dato bueno.
struct Veredicto {
    Decision decision = Decision::MAINTAIN_CAPACITY;
    Trigger trigger = Trigger::NONE;
    int desiredObjetivo = 0;
    std::string justificacion;
    // Vacio cuando trigger es NONE: si ninguna señal pidio cambio, no hay guardas que
    // evaluar. Con trigger distinto de NONE siempre trae la lista completa (4 o 6 guardas),
    // igual que VeredictoGuardas::evaluadas, para que el log muestre por que se bloqueo.
    std::vector<ResultadoGuarda> guardas;
    std::string bloqueadaPor;   // "" si no fue bloqueada por ninguna guarda
};

class DecisionCombiner {
public:
    DecisionCombiner(const Config& cfg, const SafetyGuards& guardas)
        : cfg_(cfg), guardas_(guardas) {}

    Veredicto combinar(const MetricSnapshot& s, const Calidad& cal,
                       Signal reactiva, std::optional<double> maCpu,
                       const ProactivePolicy::Resultado& proactiva,
                       const EstadoGuardas& est) const {
        const bool proactivaUp = proactiva.listo && proactiva.senal == Signal::UP;
        const bool proactivaDown = proactiva.listo && proactiva.senal == Signal::DOWN;
        const int desiredActual = s.desired.value_or(0);

        Veredicto v;
        v.desiredObjetivo = desiredActual;

        // --- Candidata SUBIR: basta con que una de las dos pida subir (OR) ---
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

        // --- Candidata BAJAR: exige que las dos pidan bajar (AND) ---
        if (reactiva == Signal::DOWN && proactivaDown) {
            // reactiva == DOWN implica ReactivePolicy::listo(), asi que maCpu siempre
            // tiene valor aqui: la señal DOWN no existe sin una MA calculada.
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

        // --- Nadie pidio un cambio accionable. Incluye el caso de señales que no se
        // pusieron de acuerdo (p. ej. reactiva DOWN con proactiva HOLD): bajar exige que
        // las DOS lo pidan, asi que una sola no es una solicitud que el combinador pueda
        // atender, y no hay guardas que evaluar sobre una decision que no se propuso. ---
        v.trigger = Trigger::NONE;
        v.justificacion = justificarSinCambio(reactiva, maCpu, proactiva);
        return v;
    }

private:
    static Trigger triggerDe(bool reactivaPide, bool proactivaPide) {
        if (reactivaPide && proactivaPide) return Trigger::BOTH;
        return reactivaPide ? Trigger::REACTIVE : Trigger::PROACTIVE;
    }

    static std::string dec(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << v;
        return oss.str();
    }

    static std::string detalleGuarda(const std::vector<ResultadoGuarda>& guardas, const std::string& nombre) {
        for (const auto& g : guardas) if (g.nombre == nombre) return g.detalle;
        return "";
    }

    std::string justificarSubida(const Veredicto& v, std::optional<double> maCpu,
                                 const ProactivePolicy::Resultado& p, int desiredActual) const {
        std::vector<std::string> motivos;
        if (v.trigger == Trigger::REACTIVE || v.trigger == Trigger::BOTH) {
            motivos.push_back("MA_CPU " + dec(*maCpu) + " > UMBRAL_ALTO " + dec(cfg_.umbralAlto));
        }
        if (v.trigger == Trigger::PROACTIVE || v.trigger == Trigger::BOTH) {
            motivos.push_back("pronostico " + dec(p.pronosticoRpm) + " RPM exige " + std::to_string(p.necesarias)
                              + " instancias (actual " + std::to_string(desiredActual) + ")");
        }
        std::ostringstream j;
        j << motivos[0];
        for (size_t i = 1; i < motivos.size(); i++) j << " y " << motivos[i];

        if (v.decision == Decision::INCREASE_CAPACITY) {
            j << "; sube a " << v.desiredObjetivo;
        } else {
            j << "; bloqueada por " << v.bloqueadaPor << ": " << detalleGuarda(v.guardas, v.bloqueadaPor);
        }
        return j.str();
    }

    std::string justificarBajada(const Veredicto& v, double maCpu,
                                 const ProactivePolicy::Resultado& p, int desiredActual) const {
        std::ostringstream j;
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

    std::string justificarSinCambio(Signal reactiva, std::optional<double> maCpu,
                                    const ProactivePolicy::Resultado& p) const {
        std::ostringstream j;
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
