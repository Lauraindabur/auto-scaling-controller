#pragma once
#include <string>
#include <aws/autoscaling/AutoScalingClient.h>

struct ResultadoAccion {
    bool exito;
    std::string mensaje;   // motivo del error, o "OK" si exito es true
};

// SetDesiredCapacity sobre el ASG (seccion 4.9), con HonorCooldown=false porque el
// cooldown es nuestro. Los limites [MIN, MAX] y el paso ±1 ya los aplico el combinador
// antes de llegar aqui: el actuador solo ejecuta.
class ASGActuator {
public:
    ASGActuator(std::string asgName, const std::string& region);

    // desired ABSOLUTO, ya calculado por el combinador (Veredicto::desiredObjetivo).
    ResultadoAccion fijarCapacidad(int desired);

private:
    std::string asgName_;
    Aws::AutoScaling::AutoScalingClient asClient_;
};
