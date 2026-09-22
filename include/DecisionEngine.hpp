#pragma once
#include "MovingAverage.hpp"
#include "CooldownManager.hpp"
#include "Config.hpp"
#include "IMetricSource.hpp"
#include "IActuator.hpp"
// Se define que recibe al crearse y que guarda

//Se pone class no como invclude para no traer solo su contenido, pues aca solo se usa  por referencia de los objetos del main.cpp
class Logger;

class DecisionEngine {
public:
    DecisionEngine(const Config& cfg, IMetricSource& metrics,
                   MovingAverage& maCpu, MovingAverage& maRt,
                   CooldownManager& cooldown, IActuator& actuator, Logger& logger)
        : cfg_(cfg), metrics_(metrics), maCpu_(maCpu), maRt_(maRt),
          cooldown_(cooldown), actuator_(actuator), logger_(logger) {}

    void ejecutarCiclo();

private:
    const Config& cfg_;
    IMetricSource& metrics_;
    MovingAverage& maCpu_;
    MovingAverage& maRt_;
    CooldownManager& cooldown_;
    IActuator& actuator_;
    Logger& logger_;
    int ciclosEnProgreso_ = 0;   // ciclos que lleva el tick actual sin confirmarse
};