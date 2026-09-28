#pragma once
#include <string>
#include <aws/autoscaling/AutoScalingClient.h>

using namespace std;

// Resultado de pedirle un cambio de capacidad a AWS
// si funcionó y, si no, por que.
struct ResultadoAccion {
    bool exito;
    string mensaje;
};

// Ejecuta en AWS la capacidad que ya decidio el controller  con SetDesiredCapacity sobre el ASG.
// No valida nada-> el numero que recibe ya viene entre el minimo (1) y el maximo (5) de instancias.

class ASGActuator {
public:
    ASGActuator(string asgName, const string& region);

    ResultadoAccion fijarCapacidad(int desired);

private:
    string asgName_;
    Aws::AutoScaling::AutoScalingClient asClient_;
};
