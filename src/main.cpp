// #include <aws/core/Aws.h>
// #include <thread>
// #include <chrono>
// #include "Config.hpp"
// #include "MovingAverage.hpp"
// #include "CooldownManager.hpp"
// #include "MetricSource.hpp"
// #include "ASGActuator.hpp"
// #include "Logger.hpp"
// #include "DecisionEngine.hpp"

using namespace std;

int main() {
    Config cfg = cargarConfigDesdeEntorno(); // si falta algo, para aca, antes de tocar AWS

    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        MetricSource metrics(cfg.asgName, cfg.region);
        MovingAverage ma(cfg.ventanaMA);
        CooldownManager cooldown(cfg.cooldownCiclos);
        ASGActuator actuator(cfg.asgName, cfg.region, cfg.capacidadMin, cfg.capacidadMax);
        Logger logger("logs/decisions.jsonl");

        // cold start: prellenar con historial real antes del bucle
        auto historial = metrics.obtenerHistorialInicial(cfg.ventanaMA);
        ma.prellenar(historial);

        DecisionEngine engine(cfg, metrics, ma, cooldown, actuator, logger);

        while (true) {
            engine.ejecutarCiclo();
            this_thread::sleep_for(chrono::seconds(cfg.intervaloCicloSegundos));
        }
    }
    Aws::ShutdownAPI(options);
    return 0;
}