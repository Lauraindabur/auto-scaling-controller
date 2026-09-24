#include "CooldownManager.hpp"
#include "test_helper.hpp"

// Simula un ciclo del DecisionEngine (ver DecisionEngine.cpp): si el cooldown
// esta activo se decrementa y se salta la evaluacion. Devuelve true si el
// ciclo pudo evaluar normalmente.
static bool cicloEvalua(CooldownManager& c) {
    if (c.cooldownActivo()) { c.decrementarCooldown(); return false; }
    return true;
}

int main() {
    {
        CooldownManager c(3);
        CHECK(c.estado() == OperationState::NONE, "inicial: estado NONE");
        CHECK(!c.cooldownActivo(), "inicial: sin cooldown");
    }
    {
        CooldownManager c(3);
        c.marcarEnProgreso();
        CHECK(c.estado() == OperationState::IN_PROGRESS, "al disparar: estado IN_PROGRESS");
        CHECK(!c.cooldownActivo(), "al disparar: el cooldown NO empieza todavia");
        c.decrementarCooldown();
        CHECK(!c.cooldownActivo(), "IN_PROGRESS: decrementar no altera nada");
        CHECK(c.estado() == OperationState::IN_PROGRESS, "IN_PROGRESS se mantiene hasta confirmar");
    }
    {
        CooldownManager c(3);
        c.marcarEnProgreso();
        c.marcarExitosa();
        CHECK(c.estado() == OperationState::NONE, "exito: estado vuelve a NONE");
        CHECK(c.cooldownActivo(), "exito: el cooldown empieza");
        CHECK(!cicloEvalua(c), "cooldown ciclo 1: no evalua");
        CHECK(!cicloEvalua(c), "cooldown ciclo 2: no evalua");
        CHECK(!cicloEvalua(c), "cooldown ciclo 3: no evalua");
        CHECK(!c.cooldownActivo(), "tras 3 ciclos: cooldown terminado");
        CHECK(cicloEvalua(c), "ciclo 4: vuelve a evaluar normal");
        c.decrementarCooldown();
        CHECK(!c.cooldownActivo(), "decrementar en 0 no baja de 0");
    }
    {
        CooldownManager c(3);
        c.marcarEnProgreso();
        c.marcarFallida();
        CHECK(c.estado() == OperationState::NONE, "fallo: estado vuelve a NONE");
        CHECK(!c.cooldownActivo(), "fallo: sin cooldown");
        CHECK(cicloEvalua(c), "fallo: siguiente ciclo evalua de inmediato");
    }

    {
        CooldownManager c(3);
        c.marcarEnProgreso();
        c.marcarTimeout();
        CHECK(c.estado() == OperationState::NONE, "timeout: estado vuelve a NONE");
        CHECK(c.cooldownActivo(), "timeout: a diferencia de un fallo de API, SI arranca cooldown");
        CHECK(!cicloEvalua(c) && !cicloEvalua(c) && !cicloEvalua(c), "timeout: 3 ciclos de cooldown sin evaluar");
        CHECK(cicloEvalua(c), "timeout: el ciclo 4 vuelve a evaluar");
    }

    std::cout << (fallos() ? "RESULTADO: FALLO\n" : "RESULTADO: OK\n");
    return fallos() ? 1 : 0;
}
