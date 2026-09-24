#include <aws/core/Aws.h>
#include <chrono>
#include <iostream>
#include <thread>
#include "ASGActuator.hpp"
#include "CloudWatchMetricSource.hpp"
#include "Config.hpp"
#include "DecisionCombiner.hpp"
#include "DecisionEngine.hpp"
#include "HoltForecaster.hpp"
#include "Logger.hpp"
#include "ProactivePolicy.hpp"
#include "ReactivePolicy.hpp"
#include "SafetyGuards.hpp"
#include "StateStore.hpp"

int main() {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    int codigoSalida = 0;
    try {
        Config cfg = cargarConfigDesdeEntorno();
        {
            CloudWatchMetricSource metrics(cfg.asgName, cfg.loadBalancerDim, cfg.targetGroupDim,
                                           cfg.region, cfg.periodoSeg);
            ASGActuator actuator(cfg.asgName, cfg.region);
            Logger logger(cfg.logFile);
            StateStore stateStore(cfg.stateFile);

            HoltForecaster holt(cfg.holtAlpha, cfg.holtBeta);
            ReactivePolicy reactiva(cfg.umbralAlto, cfg.umbralBajo, cfg.maVentana);
            ProactivePolicy proactiva(holt, cfg.cRpm, cfg.horizontePeriodos, cfg.minCapacity, cfg.maxCapacity);
            SafetyGuards guardas(cfg);
            DecisionCombiner combinador(cfg, guardas);

            // Recalienta desde el ultimo estado guardado (seccion 4.10). Si no hay
            // archivo o esta corrupto, arranca en frio: EstadoControlador vacio no rompe
            // ningun restaurar() (ya probado en el Paso 7, secuencia 11).
            EstadoControlador estado = stateStore.cargar();
            if (!stateStore.ultimaCargaValida()) {
                std::cerr << "Aviso: " << stateStore.ultimoMotivoCarga() << std::endl;
            }
            reactiva.restaurar(estado.ventanaMaCpu);
            holt.restaurar(estado.holt);
            proactiva.restaurar(estado.demanda);

            DecisionEngine engine(cfg, metrics, actuator, logger, stateStore, holt,
                                  reactiva, proactiva, guardas, combinador);
            engine.restaurar(estado);

            // Se consulta cada POLL_INTERVAL_SEG, pero el engine solo decide (y solo
            // escribe una linea de log) cuando llega un periodo de CloudWatch nuevo
            // (seccion 4.3). El reloj real solo entra aqui, como parametro: la logica de
            // decision nunca lo lee directamente.
            while (true) {
                const auto ahora = std::chrono::system_clock::now().time_since_epoch();
                const DataTs tsReloj = static_cast<DataTs>(
                    std::chrono::duration_cast<std::chrono::seconds>(ahora).count());
                engine.procesarSiHayDatoNuevo(tsReloj);
                std::this_thread::sleep_for(std::chrono::seconds(cfg.pollIntervalSeg));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error al iniciar el controller: " << e.what() << std::endl;
        codigoSalida = 1;
    }
    Aws::ShutdownAPI(options);
    return codigoSalida;
}
