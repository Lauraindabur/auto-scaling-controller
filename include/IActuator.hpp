#pragma once
#include <string>

// Interfaz mínima que DecisionEngine necesita del actuador sobre el ASG.
// Sin dependencias del AWS SDK, para poder sustituirla por un fake en pruebas.
class IActuator {
public:
    virtual ~IActuator() = default;

    virtual int capacidadActual() = 0;
    // paso: instancias a sumar en INCREASE_CAPACITY (REDUCE_CAPACITY siempre resta 1).
    virtual bool ejecutar(const std::string& decision, int paso) = 0;
    virtual bool instanciasRestantesSanas() = 0;
    virtual bool operacionTermino() = 0;
};
