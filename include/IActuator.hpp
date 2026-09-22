#pragma once
#include <string>

// Interfaz mínima que DecisionEngine necesita del ASGActuator -> donde se habla con AWS.
// Se hace para tener 0 dependencias del AWS SDK, para poder sustituirla por un fake en pruebas.
class IActuator {
public:
    virtual ~IActuator() = default;

    virtual int capacidadActual() = 0;
    //En la clase se define paso= cuantas instancias sumar -> da true si AWS acepta 
    //solo para INCREASE porque REDUCE siempre resta de a 1
    virtual bool ejecutar(const std::string& decision, int paso) = 0;
    virtual bool instanciasRestantesSanas() = 0;  // Pregunta si estan sanaas todas las instancias del TG
    virtual bool operacionTermino() = 0; //Revisar si la opercion termina y ya esta en healthy
};
