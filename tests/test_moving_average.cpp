#include "MovingAverage.hpp"
#include "test_helper.hpp"

int main() {
    MovingAverage ma(3);
    CHECK(ma.muestrasActuales() == 0, "vacio: 0 muestras");
    CHECK(!ma.tieneSuficienteHistorial(), "vacio: sin historial suficiente");

    ma.agregar(20);
    CHECK(!ma.tieneSuficienteHistorial(), "ciclo 1: sin historial suficiente");
    ma.agregar(25);
    CHECK(!ma.tieneSuficienteHistorial(), "ciclo 2: sin historial suficiente");
    CHECK(ma.muestrasActuales() == 2, "ciclo 2: 2 muestras");

    ma.agregar(22);
    CHECK(ma.tieneSuficienteHistorial(), "ciclo 3: historial completo");
    CHECK_NEAR(ma.valor(), 22.33, 0.01, "ciclo 3 = 22.33");

    ma.agregar(75);
    CHECK_NEAR(ma.valor(), 40.67, 0.01, "ciclo 4 = 40.67");
    ma.agregar(85);
    CHECK_NEAR(ma.valor(), 60.67, 0.01, "ciclo 5 = 60.67");
    ma.agregar(90);
    CHECK_NEAR(ma.valor(), 83.33, 0.01, "ciclo 6 = 83.33");
    CHECK(ma.muestrasActuales() == 3, "la ventana nunca supera 3 muestras");

    MovingAverage pre(3);
    pre.prellenar({20, 25, 22});
    CHECK(pre.tieneSuficienteHistorial(), "prellenar: historial completo");
    CHECK_NEAR(pre.valor(), 22.33, 0.01, "prellenar: promedio = 22.33");

    std::cout << (fallos() ? "RESULTADO: FALLO\n" : "RESULTADO: OK\n");
    return fallos() ? 1 : 0;
}
