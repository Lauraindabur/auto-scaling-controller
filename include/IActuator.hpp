#pragma once
#include <string>

// Interfaz mínima que DecisionEngine necesita del actuador sobre el ASG.
// Sin dependencias del AWS SDK, para poder sustituirla por un fake en pruebas.
class IActuator {
public:
    virtual ~IActuator() = default;

    virtual int capacidadActual() = 0;
    virtual bool ejecutar(const std::string& decision) = 0;
    virtual bool instanciasRestantesSanas() = 0;
    virtual bool operacionTermino() = 0;
};
