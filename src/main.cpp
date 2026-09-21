#include <aws/core/Aws.h>
#include <thread>
#include <chrono>
#include <iostream>
#include "Config.hpp"
#include "MovingAverage.hpp"
#include "CooldownManager.hpp"
#include "MetricSource.hpp"
#include "ASGActuator.hpp"
#include "Logger.hpp"
#include "DecisionEngine.hpp"

int main() {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    int codigoSalida = 0;
    try {
        Config cfg = cargarConfigDesdeEntorno();
        {
            MetricSource metrics(cfg.asgName, cfg.loadBalancerArn, cfg.targetGroupArn, cfg.region);
            MovingAverage maCpu(cfg.ventanaMA);
            MovingAverage maRt(cfg.ventanaMA);
            CooldownManager cooldown(cfg.cooldownCiclos);
            ASGActuator actuator(cfg.asgName, cfg.region, cfg.targetGroupArn,
                                  cfg.capacidadMin, cfg.capacidadMax);
            Logger logger("logs/decisions.jsonl");

            maCpu.prellenar(metrics.obtenerHistorialInicial(cfg.ventanaMA));
            maRt.prellenar(metrics.obtenerHistorialInicialRT(cfg.ventanaMA));

            DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);

            while (true) {
                engine.ejecutarCiclo();
                std::this_thread::sleep_for(std::chrono::seconds(cfg.intervaloCicloSegundos));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error al iniciar el controller: " << e.what() << std::endl;
        codigoSalida = 1;
    }
    Aws::ShutdownAPI(options);
    return codigoSalida;
}
