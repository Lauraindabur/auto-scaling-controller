#pragma once
#include "MovingAverage.hpp"
#include "CooldownManager.hpp"
#include "Config.hpp"
#include "IMetricSource.hpp"
#include "IActuator.hpp"

class Logger;

class DecisionEngine {
public:
    DecisionEngine(const Config& cfg, IMetricSource& metrics, MovingAverage& ma,
                   CooldownManager& cooldown, IActuator& actuator, Logger& logger)
        : cfg_(cfg), metrics_(metrics), ma_(ma),
          cooldown_(cooldown), actuator_(actuator), logger_(logger) {}

    void ejecutarCiclo();

private:
    const Config& cfg_;
    IMetricSource& metrics_;
    MovingAverage& ma_;
    CooldownManager& cooldown_;
    IActuator& actuator_;
    Logger& logger_;
};