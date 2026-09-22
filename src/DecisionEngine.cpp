#include "DecisionEngine.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cmath>

// funcion que me ayuda a que se converta de enum a texto para ponerlo en los logs
static const char* nombreEstado(OperationState e) {
    switch (e) {
        case OperationState::NONE:        return "NONE";
        case OperationState::IN_PROGRESS: return "IN_PROGRESS";
        case OperationState::SUCCESSFUL:  return "SUCCESSFUL";
        case OperationState::FAILED:      return "FAILED";
    }
    return "NONE";
}
// Se crea RegistroCiclo del logger para ir acumulando datos y a final que se escriba como una linea del log
void DecisionEngine::ejecutarCiclo() {
    RegistroCiclo reg;
    bool operacionFallo = false;   // solo valdría true en el ciclo en que se declara el timeout

    // Primero guardar el registro con su decision, motivo
    auto registrar = [&](const std::string& decision, const std::string& justificacion) {
        reg.decision = decision;
        reg.justificacion = justificacion;
        reg.estadoOperacion = operacionFallo ? "FAILED" : nombreEstado(cooldown_.estado());  //Si operacionFallo es true poner FAILED : estado real
        reg.cooldownRestante = cooldown_.ciclosRestantes();
        logger_.registrar(reg);
    };

    // Las dos metricas se muestran juntas en todos los ciclos. Si falla cualquiera,el ciclo entero es fallo y no se toca ningun buffer.
    auto lectura = metrics_.obtenerActual();
    // Si lecutra no fue extiosa, entones se escribe la decision,motivo
    if (!lectura.exito) {
        std::string motivo = !lectura.cpuOk && !lectura.rtOk
                                 ? "CPUUtilization y TargetResponseTime no disponibles"
                             : !lectura.cpuOk ? "CPUUtilization no disponible"
                                              : "TargetResponseTime no disponible";
        registrar("MAINTAIN_CAPACITY", motivo + " tras agotar reintentos");
        return;
    }
    // Si se llega a esta aprte es porque la lectura fue exito, y se guarda en registro
    reg.cpuUtilization = lectura.cpu;
    reg.targetResponseTime = lectura.responseTime;
    //Ahora se grega la elctua el MA de cada una
    maCpu_.agregar(lectura.cpu);
    maRt_.agregar(lectura.responseTime);
    // Si alguna de las 2 no tiene todaia als 3 muestras lo gaurda con deicision, motivo
    if (!maCpu_.tieneSuficienteHistorial() || !maRt_.tieneSuficienteHistorial()) {
        std::string faltan = !maCpu_.tieneSuficienteHistorial() && !maRt_.tieneSuficienteHistorial()
                                 ? "MA_CPU y MA_RT"
                             : !maCpu_.tieneSuficienteHistorial() ? "MA_CPU" : "MA_RT";
        registrar("MAINTAIN_CAPACITY", "historial insuficiente: " + faltan);
        return;
    }

    // Se asume que ya tiene 3 cada una -> maCpu es para calucalr los promedios en las variables lcoales -> ps maCpu es objeto de la caske 
    double maCpu = maCpu_.valor();
    double maRt = maRt_.valor();
    // Se guardaan en el archivo del log
    reg.movingAverageCpu = maCpu;
    reg.movingAverageResponseTime = maRt;

    // Se compara si hay alguna operacion de escalado en curso
    if (cooldown_.estado() == OperationState::IN_PROGRESS) {
        ciclosEnProgreso_++;
        if (actuator_.operacionTermino()) {
            // Confirmada: el cooldown arranca ahora y este ciclo no cuenta como parte de él.
            cooldown_.marcarExitosa();
            reg.resultadoAccion = "SUCCESSFUL";
            registrar("MAINTAIN_CAPACITY", "operación confirmada, inicia cooldown");
        } else if (ciclosEnProgreso_ > cfg_.timeoutOperacionCiclos) {
            // Timeout: no se deshace nada. Se da por fallida y se espera el cooldown normal.
            cooldown_.marcarTimeout();
            operacionFallo = true;
            reg.resultadoAccion = "FAILED";
            registrar("MAINTAIN_CAPACITY", "operación excedió el tiempo máximo ("
                      + std::to_string(cfg_.timeoutOperacionCiclos) + " ciclos), se marca como fallida");
        } else {
            registrar("MAINTAIN_CAPACITY", "operación en curso");
        }
        return;
    }

    if (cooldown_.cooldownActivo()) {
        cooldown_.decrementarCooldown();
        registrar("MAINTAIN_CAPACITY", "en periodo de cooldown");
        return;
    }

    int capacidad = actuator_.capacidadActual();
    if (capacidad < 0) {
        registrar("MAINTAIN_CAPACITY", "capacidad actual no disponible");
        return;
    }
    reg.capacidadActual = capacidad;
    std::string decision, justificacion;
    int paso = 1;   // instancias a sumar si se decide subir

    bool subidaCpu = maCpu > cfg_.umbralSubida;
    bool subidaRt = maRt > cfg_.umbralRtSubida;
    bool bajadaCpu = maCpu < cfg_.umbralBajada;
    bool bajadaRt = maRt < cfg_.umbralRtBajada;

    if (subidaCpu || subidaRt) {
        // SUBIR: cualquiera de las dos metricas basta.
        std::string motivo;
        if (subidaCpu && subidaRt) {
            reg.decisionTrigger = "CPU+RT";
            motivo = "MA_CPU y MA_RT por encima de sus umbrales de subida";
        } else if (subidaCpu) {
            reg.decisionTrigger = "CPU";
            motivo = "MA_CPU por encima del umbral de subida";
        } else {
            reg.decisionTrigger = "RT";
            motivo = "MA_RT por encima del umbral de subida";
        }
        if (capacidad >= cfg_.capacidadMax) {
            decision = "MAINTAIN_CAPACITY"; justificacion = "límite máximo alcanzado (" + motivo + ")";
        } else {
            decision = "INCREASE_CAPACITY";
            if (subidaRt) {
                // Subida proporcional: N_necesarias = techo(N_actual * MA_RT / umbral_RT).
                // Si solo disparo la CPU no hay modelo medido, y el paso se queda en +1.
                int necesarias = static_cast<int>(std::ceil(capacidad * maRt / cfg_.umbralRtSubida));
                necesarias = std::min(necesarias, cfg_.capacidadMax);   // tope 1: capacidad maxima
                paso = necesarias - capacidad;
                paso = std::min(paso, cfg_.pasoMaximoSubida);           // tope 2: paso maximo
                paso = std::max(paso, 1);                               // nunca menos de +1
            }
            justificacion = motivo + "; capacidad objetivo " + std::to_string(capacidad + paso)
                            + ", paso +" + std::to_string(paso);
        }
    } else if (bajadaCpu && bajadaRt) {
        // BAJAR: conservador, exige CPU baja Y tiempo de respuesta bajo.
        reg.decisionTrigger = "CPU+RT";
        const std::string motivo = "MA_CPU y MA_RT por debajo de sus umbrales de bajada";
        if (capacidad <= cfg_.capacidadMin) {
            decision = "MAINTAIN_CAPACITY"; justificacion = "límite mínimo alcanzado (" + motivo + ")";
        } else if (!actuator_.instanciasRestantesSanas()) {
            decision = "MAINTAIN_CAPACITY"; justificacion = "no seguro reducir";
        } else {
            decision = "REDUCE_CAPACITY"; justificacion = motivo;
        }
    } else if (bajadaCpu) {
        decision = "MAINTAIN_CAPACITY";
        justificacion = "MA_CPU baja pero MA_RT no está por debajo del umbral de bajada";
    } else {
        decision = "MAINTAIN_CAPACITY"; justificacion = "dentro del rango esperado";
    }

    if (decision != "MAINTAIN_CAPACITY") {
        int objetivo = decision == "INCREASE_CAPACITY" ? capacidad + paso : capacidad - 1;
        reg.accionSolicitada = decision + " -> " + std::to_string(objetivo);
        bool exito = actuator_.ejecutar(decision, paso);
        if (exito) { cooldown_.marcarEnProgreso(); ciclosEnProgreso_ = 0; reg.resultadoAccion = "IN_PROGRESS"; }
        else { cooldown_.marcarFallida(); reg.resultadoAccion = "FAILED"; }
    }

    auto secundarias = metrics_.obtenerMetricasSecundarias();
    if (secundarias.exito) {
        reg.requestCountPerTarget = secundarias.requestCountPerTarget;
    }
    registrar(decision, justificacion);
}
