#pragma once
#include "MovingAverage.hpp"
#include "CooldownManager.hpp"
#include "Config.hpp"

class MetricSource;
class ASGActuator;
class Logger;

class DecisionEngine {
public:
    DecisionEngine(const Config& cfg, MetricSource& metrics, MovingAverage& ma,
                   CooldownManager& cooldown, ASGActuator& actuator, Logger& logger)
        : cfg_(cfg), metrics_(metrics), ma_(ma),
          cooldown_(cooldown), actuator_(actuator), logger_(logger) {}

    void ejecutarCiclo();

private:
    const Config& cfg_;
    MetricSource& metrics_;
    MovingAverage& ma_;
    CooldownManager& cooldown_;
    ASGActuator& actuator_;
    Logger& logger_;
};