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

// Estado que las guardas necesitan recordar entre ciclos. Lo persiste el StateStore y se
// mide todo contra el timestamp del dato, no contra el reloj del sistema.
struct EstadoGuardas {
    std::optional<DataTs> tsUltimaSubida;
    std::optional<DataTs> tsUltimaBajada;
    // Desde cuando hay capacidad en camino, para el timeout de warm-up. Lo mantiene el
    // engine: se fija al detectar la condicion y se limpia cuando desaparece.
    std::optional<DataTs> tsWarmupDesde;
};

struct ResultadoGuarda {
    std::string nombre;
    bool paso = false;
    std::string detalle;
    // WARN: la guarda habria bloqueado pero dejo de hacerlo (timeout de warm-up).
    bool advertencia = false;
};

struct VeredictoGuardas {
    bool permitido = false;
    std::string bloqueadaPor;                 // "" si permitido
    // TODAS las guardas evaluadas, en orden, con su resultado. No solo la que bloqueo:
    // el log tiene que dejar ver cual esta frenando al controller en la practica, que es
    // lo que permite ajustar los umbrales despues del experimento.
    std::vector<ResultadoGuarda> evaluadas;
};

class SafetyGuards {
public:
    explicit SafetyGuards(const Config& cfg) : cfg_(cfg) {}

    // "Hay capacidad en camino": solo instancias activamente arrancando (Pending > 0). El
    // engine usa esto para mantener EstadoGuardas::tsWarmupDesde. HealthyHostCount <
    // InService sin Pending NO cuenta aqui: es una caida sin reemplazo (todo lo que habia
    // se cayo y nada nuevo se esta lanzando), no un warm-up en progreso, y bloquear la
    // subida ahi es contraproducente (bug confirmado 25-sep-2026: cpu=100%, sanos=0,
    // inService=1, pending=0, warmup bloqueaba la subida indefinidamente).
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
            guarda(v, "limite_maximo", ok, "desired " + std::to_string(*s.desired)
                   + (ok ? " < " : " >= ") + "MAX " + std::to_string(cfg_.maxCapacity));
        }

        // Cooldown de subida: al menos el warm-up, para no apilar instancias.
        if (!est.tsUltimaSubida.has_value()) {
            guarda(v, "cooldown_subida", true, "sin subidas previas");
        } else {
            const DataTs transcurrido = s.dataTs - *est.tsUltimaSubida;
            const bool ok = transcurrido >= static_cast<DataTs>(cfg_.cooldownSubidaSeg);
            guarda(v, "cooldown_subida", ok, std::to_string(transcurrido)
                   + " s desde la ultima subida, se exigen " + std::to_string(cfg_.cooldownSubidaSeg) + " s");
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

        // Nunca reducir con n estimado: subir de mas cuesta dinero, bajar de mas cuesta
        // disponibilidad, asi que la bajada exige haber medido los targets sanos.
        guarda(v, "hosts_sanos_medidos", !cal.hostsSanosEstimado,
               cal.hostsSanosEstimado
                   ? "n viene de InService, no de HealthyHostCount: no se reduce a ciegas"
                   : "n medido con HealthyHostCount");

        const int n = cal.hostsSanos.value_or(0);

        // Limite inferior.
        if (!cal.hostsSanos.has_value()) {
            guarda(v, "limite_minimo", true, "no aplica: n desconocido");
        } else {
            const bool ok = n > cfg_.minCapacity;
            guarda(v, "limite_minimo", ok, "n " + std::to_string(n)
                   + (ok ? " > " : " <= ") + "MIN " + std::to_string(cfg_.minCapacity));
        }

        // Cooldown de bajada, contra la ultima accion de cualquier tipo: no se baja poco
        // despues de haber subido, que es la principal fuente de oscilacion.
        {
            std::optional<DataTs> ultimaAccion = est.tsUltimaSubida;
            if (est.tsUltimaBajada.has_value()) {
                ultimaAccion = ultimaAccion.has_value()
                                   ? std::max(*ultimaAccion, *est.tsUltimaBajada)
                                   : est.tsUltimaBajada;
            }
            if (!ultimaAccion.has_value()) {
                guarda(v, "cooldown_bajada", true, "sin acciones previas");
            } else {
                const DataTs transcurrido = s.dataTs - *ultimaAccion;
                const bool ok = transcurrido >= static_cast<DataTs>(cfg_.cooldownBajadaSeg);
                guarda(v, "cooldown_bajada", ok, std::to_string(transcurrido)
                       + " s desde la ultima accion (subida o bajada), se exigen "
                       + std::to_string(cfg_.cooldownBajadaSeg) + " s");
            }
        }

        // Chequeo n-1: simular que ya se retiro una instancia y exigir que el resultado
        // siga siendo sostenible, por CPU y por demanda pronosticada.
        if (n < 2) {
            const std::string motivo = "no aplica: n < 2 (limite_minimo ya bloquea)";
            guarda(v, "chequeo_n1_cpu", true, motivo);
            guarda(v, "chequeo_n1_pronostico", true, motivo);
        } else {
            const double cpuProyectada = maCpu * n / (n - 1);
            const double umbralProyectado = cfg_.umbralAlto - cfg_.margenBajada;
            guarda(v, "chequeo_n1_cpu", cpuProyectada < umbralProyectado,
                   "CPU proyectada con " + std::to_string(n - 1) + " instancias = " + dec(cpuProyectada)
                   + " %, umbral " + dec(umbralProyectado) + " %");

            const double porInstancia = pronosticoRpm / (n - 1);
            guarda(v, "chequeo_n1_pronostico", porInstancia < cfg_.cRpm,
                   "pronostico por instancia con " + std::to_string(n - 1) + " = " + dec(porInstancia)
                   + " RPM, capacidad " + dec(cfg_.cRpm) + " RPM");
        }

        cerrar(v);
        return v;
    }

private:
    static std::string dec(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << v;
        return oss.str();
    }

    static void guarda(VeredictoGuardas& v, const std::string& nombre, bool paso,
                       const std::string& detalle, bool advertencia = false) {
        v.evaluadas.push_back({nombre, paso, detalle, advertencia});
    }

    static void agregarDatosCompletos(VeredictoGuardas& v, const Calidad& cal) {
        std::string detalle = "dato COMPLETE";
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
        const std::string estadoActual = "pending=" + std::to_string(s.pending.value_or(0))
            + ", sanos=" + (cal.hostsSanos.has_value() ? std::to_string(*cal.hostsSanos) : "?")
            + ", inService=" + (s.inService.has_value() ? std::to_string(*s.inService) : "?");

        // Timeout: una instancia que nunca llega a sana no puede bloquear el escalado para
        // siempre. Se avisa y se deja pasar, asumiendo que esa capacidad no va a aparecer.
        if (esperando >= static_cast<DataTs>(cfg_.warmupTimeoutSeg)) {
            guarda(v, "warmup", true, "WARN: capacidad en camino desde hace " + std::to_string(esperando)
                   + " s, por encima del timeout de " + std::to_string(cfg_.warmupTimeoutSeg)
                   + " s (" + estadoActual + "): se asume instancia atascada y la guarda deja de bloquear",
                   /*advertencia=*/true);
        } else {
            guarda(v, "warmup", false, "capacidad en camino desde hace " + std::to_string(esperando)
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
