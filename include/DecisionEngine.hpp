#pragma once
#include <optional>
#include "ASGActuator.hpp"
#include "CloudWatchMetricSource.hpp"
#include "Config.hpp"
#include "DecisionCombiner.hpp"
#include "HoltForecaster.hpp"
#include "Logger.hpp"
#include "MetricSnapshot.hpp"
#include "ProactivePolicy.hpp"
#include "ReactivePolicy.hpp"
#include "SafetyGuards.hpp"
#include "StateStore.hpp"

using namespace std;

// Orquesta un ciclo completo -> pide el dato a CloudWatch, evalua su calidad, actualiza las
// politicas, corre las safetyguards y el combinador, ejecuta la decision en el ASG y la deja
// registrada en el log y en el estado guardado.
class DecisionEngine {
public:
    DecisionEngine(const Config& cfg, CloudWatchMetricSource& metrics, ASGActuator& actuator,
                   Logger& logger, StateStore& stateStore, HoltForecaster& holt,
                   ReactivePolicy& reactiva, ProactivePolicy& proactiva,
                   SafetyGuards& guardas, DecisionCombiner& combinador)
        : cfg_(cfg), metrics_(metrics), actuator_(actuator), logger_(logger),
          stateStore_(stateStore), holt_(holt), reactiva_(reactiva), proactiva_(proactiva),
          guardas_(guardas), combinador_(combinador) {}

    // Se retoma un estado cargado de StateStore -> el ultimo dato procesado y sus estados
    void restaurar(const EstadoControlador& e) {
        tsUltimoProcesado_ = e.tsUltimoDatoProcesado;
        estadoGuardas_.tsUltimaSubida = e.tsUltimaSubida;
        estadoGuardas_.tsUltimaBajada = e.tsUltimaBajada;
    }

    // true -> se procesó un ciclo completo y se escribió en logs, false si no hay dato nuevo 
    bool procesarSiHayDatoNuevo(DataTs tsReloj) {
        optional<MetricSnapshot> snapshot = metrics_.ultimoPeriodoCompleto();
        if (!snapshot.has_value()) return false;
        if (tsUltimoProcesado_.has_value() && snapshot->dataTs <= *tsUltimoProcesado_) return false;

        procesarCiclo(*snapshot, tsReloj);
        tsUltimoProcesado_ = snapshot->dataTs;
        return true;
    }

private:
    void procesarCiclo(const MetricSnapshot& s, DataTs tsReloj) {
        const Calidad cal = evaluarCalidad(s, tsReloj, cfg_.maxDataAgeSeg);

        RegistroCiclo reg;
        reg.dataTs = s.dataTs;
        reg.periodoSegundos = s.periodoSegundos;
        reg.ventanaMa = static_cast<int>(cfg_.maVentana);
        reg.cpu = s.cpu;
        reg.requestCount = s.requestCount;
        reg.healthyHosts = s.healthyHosts;
        reg.calidad = cal.nivel;
        reg.dataIssues = cal.problemas;
        reg.desired = s.desired;
        reg.inService = s.inService;
        reg.pending = s.pending;
        reg.horizon = cfg_.horizontePeriodos;

        // Acá solo un dato COMPLETO alimenta las politicas, si hay un ciclo incomplto no entra al MA 
        if (cal.completo()) {
            reactiva_.observar(*s.cpu);
            proactiva_.observar(*cal.requestCountEfectivo);
        }

        reg.maCpu = reactiva_.maCpu();
        reg.reactiveSignal = reactiva_.senal();

        if (holt_.inicializado()) {
            reg.holtLevel = holt_.nivel();
            reg.holtTrend = holt_.tendencia();
        }

        const int capacidadActual = s.inService.value_or(0) + s.pending.value_or(0);
        const auto resultadoProactivo = proactiva_.evaluar(capacidadActual);
        reg.proactiveSignal = resultadoProactivo.senal;
        reg.spikeGuardApplied = resultadoProactivo.guardaPicoAplicada;
        if (resultadoProactivo.listo) {
            reg.forecastRpm = resultadoProactivo.pronosticoRpm;
            reg.neededInstances = resultadoProactivo.necesarias;
        }

        const Veredicto v = combinador_.combinar(s, cal, reactiva_.senal(), reactiva_.maCpu(),
                                                 resultadoProactivo, estadoGuardas_);
        reg.guards = v.guardas;
        reg.blockedBy = v.bloqueadaPor;
        reg.decision = v.decision;
        reg.trigger = v.trigger;
        reg.justification = v.justificacion;

        if (v.decision == Decision::INCREASE_CAPACITY || v.decision == Decision::REDUCE_CAPACITY) {
            reg.actionRequested = "SetDesiredCapacity " + to_string(s.desired.value_or(0))
                                  + "->" + to_string(v.desiredObjetivo);
            const ResultadoAccion r = actuator_.fijarCapacidad(v.desiredObjetivo);
            if (r.exito) {
                reg.actionResult = "OK";
                if (v.decision == Decision::INCREASE_CAPACITY) estadoGuardas_.tsUltimaSubida = s.dataTs;
                else estadoGuardas_.tsUltimaBajada = s.dataTs;
            } else {
                reg.actionResult = "ERROR: " + r.mensaje;
            }
        }

        // tsWarmupDesde: arranca la primera vez que se detecta capacidad en camino, y se
        // limpia en cuanto deja de haberla (SafetyGuards::capacidadEnCamino ya sabe
        // calcularlo a partir del snapshot y la calidad).
        if (guardas_.capacidadEnCamino(s, cal)) {
            if (!estadoGuardas_.tsWarmupDesde.has_value()) estadoGuardas_.tsWarmupDesde = s.dataTs;
        } else {
            estadoGuardas_.tsWarmupDesde.reset();
        }

        logger_.registrar(reg);

        EstadoControlador estado;
        estado.tsUltimoDatoProcesado = s.dataTs;
        estado.ventanaMaCpu = reactiva_.ventana();
        estado.holt = holt_.estado();
        estado.demanda = proactiva_.estado();
        estado.tsUltimaSubida = estadoGuardas_.tsUltimaSubida;
        estado.tsUltimaBajada = estadoGuardas_.tsUltimaBajada;
        stateStore_.guardar(estado);
    }

    const Config& cfg_;
    CloudWatchMetricSource& metrics_;
    ASGActuator& actuator_;
    Logger& logger_;
    StateStore& stateStore_;
    HoltForecaster& holt_;
    ReactivePolicy& reactiva_;
    ProactivePolicy& proactiva_;
    SafetyGuards& guardas_;
    DecisionCombiner& combinador_;

    optional<DataTs> tsUltimoProcesado_;
    EstadoGuardas estadoGuardas_;
};
