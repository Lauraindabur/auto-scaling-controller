#pragma once
#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "Config.hpp"
#include "DataQuality.hpp"
#include "MetricSnapshot.hpp"

using namespace std;

// Esta estructura guarda todo lo que las guards necesitan recordar entre ciclos 
struct EstadoGuardas {
    optional<DataTs> tsUltimaSubida;
    optional<DataTs> tsUltimaBajada;
    optional<DataTs> tsWarmupDesde; // desde cuando hay una instancia arrancando, para medir el timeout de warm-up
};

struct ResultadoGuarda {
    string nombre;
    bool paso = false;
    string detalle;
    bool advertencia = false;
};

struct VeredictoGuardas {
    bool permitido = false;
    string bloqueadaPor;                 
    vector<ResultadoGuarda> evaluadas;
};

class SafetyGuards {
public:
    explicit SafetyGuards(const Config& cfg) : cfg_(cfg) {}

    // Son solo instancias  arrancando -> Pending > 0 se usa para q el engine mantenga tsWarmupDesde y no bloquee la subida por warm-up.
    bool capacidadEnCamino(const MetricSnapshot& s, const Calidad&) const {
        return s.pending.value_or(0) > 0;
    }

    VeredictoGuardas evaluarSubida(const MetricSnapshot& s, const Calidad& cal,
                                   const EstadoGuardas& est) const {
        VeredictoGuardas v;
        agregarDatosCompletos(v, cal);

        // Limite superior: nunca por encima de MAX_CAPACITY.
        if (!s.desired.has_value()) {
            guarda(v, "limite_maximo", true, "no aplica: estado del ASG ausente");
        } else {
            const bool ok = *s.desired < cfg_.maxCapacity;
            string comparador;
            if (ok) {
                comparador = " < ";
            } else {
                comparador = " >= ";
            }
            const string detalle = "desired " + to_string(*s.desired) + comparador
                                    + "MAX " + to_string(cfg_.maxCapacity);
            guarda(v, "limite_maximo", ok, detalle);
        }

    
        if (!est.tsUltimaSubida.has_value()) {
            guarda(v, "cooldown_subida", true, "sin subidas previas");
        } else {
            const DataTs transcurrido = s.dataTs - *est.tsUltimaSubida;
            const bool ok = transcurrido >= static_cast<DataTs>(cfg_.cooldownSubidaSeg);
            guarda(v, "cooldown_subida", ok, to_string(transcurrido)
                   + " s desde la ultima subida, se exigen " + to_string(cfg_.cooldownSubidaSeg) + " s");
        }

        agregarWarmup(v, s, cal, est);
        cerrar(v);
        return v;
    }

    VeredictoGuardas evaluarBajada(const MetricSnapshot& s, const Calidad& cal,
                                   const EstadoGuardas& est,
                                   double maCpu, double pronosticoRpm) const {
        VeredictoGuardas v;
        agregarDatosCompletos(v, cal);

        // Aca se hace apra no reducir con n estimado, la idea es que para bajar se debe exigir haber medido los targets sanos 
        guarda(v, "hosts_sanos_medidos", !cal.hostsSanosEstimado,
               cal.hostsSanosEstimado
                   ? "n viene de InService, no de HealthyHostCount: no se reduce a ciegas"
                   : "n medido con HealthyHostCount");

        const int n = cal.hostsSanos.value_or(0);

        if (!cal.hostsSanos.has_value()) {
            guarda(v, "limite_minimo", true, "no aplica: n desconocido");
        } else {
            const bool ok = n > cfg_.minCapacity;
            guarda(v, "limite_minimo", ok, "n " + to_string(n)
                   + (ok ? " > " : " <= ") + "MIN " + to_string(cfg_.minCapacity));
        }

        // Se aplica el cooldown de bajada -> para bajar se debe haben pasado cooldownBajadaSeg desde la ultima accion
        {
            optional<DataTs> ultimaAccion = est.tsUltimaSubida;
            if (est.tsUltimaBajada.has_value()) {
                ultimaAccion = ultimaAccion.has_value()
                                   ? max(*ultimaAccion, *est.tsUltimaBajada)
                                   : est.tsUltimaBajada;
            }
            if (!ultimaAccion.has_value()) {
                guarda(v, "cooldown_bajada", true, "sin acciones previas");
            } else {
                const DataTs transcurrido = s.dataTs - *ultimaAccion;
                const bool ok = transcurrido >= static_cast<DataTs>(cfg_.cooldownBajadaSeg);
                guarda(v, "cooldown_bajada", ok, to_string(transcurrido)
                       + " s desde la ultima accion (subida o bajada), se exigen "
                       + to_string(cfg_.cooldownBajadaSeg) + " s");
            }
        }

        if (n < 2) {
            const string motivo = "no aplica: n < 2 (limite_minimo ya bloquea)";
            guarda(v, "chequeo_n1_cpu", true, motivo);
            guarda(v, "chequeo_n1_pronostico", true, motivo);
        } else {
            const double cpuProyectada = maCpu * n / (n - 1);
            const double umbralProyectado = cfg_.umbralAlto - cfg_.margenBajada;
            guarda(v, "chequeo_n1_cpu", cpuProyectada < umbralProyectado,
                   "CPU proyectada con " + to_string(n - 1) + " instancias = " + dec(cpuProyectada)
                   + " %, umbral " + dec(umbralProyectado) + " %");

            const double porInstancia = pronosticoRpm / (n - 1);
            guarda(v, "chequeo_n1_pronostico", porInstancia < cfg_.cRpm,
                   "pronostico por instancia con " + to_string(n - 1) + " = " + dec(porInstancia)
                   + " RPM, capacidad " + dec(cfg_.cRpm) + " RPM");
        }

        cerrar(v);
        return v;
    }

private:
    static string dec(double v) {
        ostringstream oss;
        oss << fixed << setprecision(2) << v;
        return oss.str();
    }

    static void guarda(VeredictoGuardas& v, const string& nombre, bool paso,
                       const string& detalle, bool advertencia = false) {
        v.evaluadas.push_back({nombre, paso, detalle, advertencia});
    }

    static void agregarDatosCompletos(VeredictoGuardas& v, const Calidad& cal) {
        string detalle = "dato COMPLETE";
        if (!cal.completo()) {
            detalle = "dato INCOMPLETE";
            for (const auto& p : cal.problemas) detalle += "; " + p;
        }
        guarda(v, "datos_completos", cal.completo(), detalle);
    }

    void agregarWarmup(VeredictoGuardas& v, const MetricSnapshot& s, const Calidad& cal,
                       const EstadoGuardas& est) const {
        if (!capacidadEnCamino(s, cal)) {
            guarda(v, "warmup", true, "sin instancias Pending y todos los targets sanos");
            return;
        }
        const DataTs desde = est.tsWarmupDesde.value_or(s.dataTs);
        const DataTs esperando = s.dataTs - desde;
        const string estadoActual = "pending=" + to_string(s.pending.value_or(0))
            + ", sanos=" + (cal.hostsSanos.has_value() ? to_string(*cal.hostsSanos) : "?")
            + ", inService=" + (s.inService.has_value() ? to_string(*s.inService) : "?");

        //Aca se toma como Timeout -> una ec2 q no lllega a sana no peude bloquear el escalado por infinito tiempo, se pasa a otra accion y se asume que esa capcidad de esa instancia no va a aperecer 
        if (esperando >= static_cast<DataTs>(cfg_.warmupTimeoutSeg)) {
            guarda(v, "warmup", true, "WARN: capacidad en camino desde hace " + to_string(esperando)
                   + " s, por encima del timeout de " + to_string(cfg_.warmupTimeoutSeg)
                   + " s (" + estadoActual + "): se asume instancia atascada y la guarda deja de bloquear",
                   true);
        } else {
            guarda(v, "warmup", false, "capacidad en camino desde hace " + to_string(esperando)
                   + " s (" + estadoActual + "): se espera a que termine");
        }
    }

    static void cerrar(VeredictoGuardas& v) {
        for (const auto& g : v.evaluadas) {
            if (!g.paso) { v.bloqueadaPor = g.nombre; break; }
        }
        v.permitido = v.bloqueadaPor.empty();
    }

    const Config& cfg_;
};
