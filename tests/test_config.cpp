#include "Config.hpp"
#include "test_helper.hpp"
#include <cstdlib>

// Variables validas (las mismas que config/controller.env, con ARN de ejemplo).
static void entornoValido() {
    setenv("ASG_NAME", "asg", 1);
    setenv("AWS_REGION", "us-east-1", 1);
    setenv("TARGET_GROUP_ARN", "arn:aws:elasticloadbalancing:us-east-1:1:targetgroup/t/1", 1);
    setenv("LOAD_BALANCER_ARN", "arn:aws:elasticloadbalancing:us-east-1:1:loadbalancer/app/lb/1", 1);
    setenv("UMBRAL_SUBIDA", "70.0", 1);
    setenv("UMBRAL_BAJADA", "30.0", 1);
    setenv("UMBRAL_RT_SUBIDA", "1.0", 1);
    setenv("UMBRAL_RT_BAJADA", "1.0", 1);
    setenv("VENTANA_MA", "3", 1);
    setenv("COOLDOWN_CICLOS", "3", 1);
    setenv("TIMEOUT_OPERACION_CICLOS", "6", 1);
    setenv("CAPACIDAD_MIN", "1", 1);
    setenv("CAPACIDAD_MAX", "5", 1);
    setenv("PASO_MAXIMO_SUBIDA", "2", 1);
    setenv("INTERVALO_CICLO_SEGUNDOS", "60", 1);
}

// Pone entorno valido, cambia una variable y comprueba que el error nombra esa variable.
static void debeFallar(const char* var, const char* valor, const char* debeMencionar, const char* nombre) {
    entornoValido();
    if (valor) setenv(var, valor, 1); else unsetenv(var);
    try {
        cargarConfigDesdeEntorno();
        CHECK(false, nombre << " (no lanzo excepcion)");
    } catch (const std::exception& e) {
        std::string msg = e.what();
        std::cout << "      -> " << msg << "\n";
        CHECK(msg.find(debeMencionar) != std::string::npos, nombre);
    }
}

int main() {
    entornoValido();
    try {
        Config c = cargarConfigDesdeEntorno();
        CHECK(c.umbralSubida == 70.0 && c.umbralBajada == 30.0 && c.ventanaMA == 3 &&
              c.capacidadMin == 1 && c.capacidadMax == 5 && c.pasoMaximoSubida == 2,
              "la configuracion real (umbrales 70/30/1.0, ventana 3, 1-5, paso 2) es valida");
        CHECK(c.umbralRtBajada == c.umbralRtSubida, "umbrales RT iguales (1.0 y 1.0) siguen siendo validos");
    } catch (const std::exception& e) {
        CHECK(false, std::string("la configuracion valida no debe fallar: ") + e.what());
    }

    debeFallar("VENTANA_MA", "0", "VENTANA_MA", "VENTANA_MA=0 se rechaza");
    debeFallar("VENTANA_MA", "-1", "VENTANA_MA", "VENTANA_MA negativa se rechaza");
    debeFallar("CAPACIDAD_MIN", "6", "CAPACIDAD_MIN", "CAPACIDAD_MIN > CAPACIDAD_MAX se rechaza");
    debeFallar("CAPACIDAD_MIN", "0", "CAPACIDAD_MIN", "CAPACIDAD_MIN = 0 se rechaza");
    debeFallar("UMBRAL_BAJADA", "80", "UMBRAL_BAJADA", "UMBRAL_BAJADA > UMBRAL_SUBIDA se rechaza");
    debeFallar("UMBRAL_BAJADA", "70", "UMBRAL_BAJADA", "UMBRAL_BAJADA = UMBRAL_SUBIDA se rechaza");
    debeFallar("UMBRAL_SUBIDA", "150", "UMBRAL_SUBIDA", "UMBRAL_SUBIDA > 100 se rechaza");
    debeFallar("UMBRAL_RT_BAJADA", "2.0", "UMBRAL_RT_BAJADA", "UMBRAL_RT_BAJADA > UMBRAL_RT_SUBIDA se rechaza");
    debeFallar("UMBRAL_RT_SUBIDA", "0", "UMBRAL_RT", "UMBRAL_RT_SUBIDA = 0 se rechaza (division por cero en la subida proporcional)");
    debeFallar("PASO_MAXIMO_SUBIDA", "0", "PASO_MAXIMO_SUBIDA", "PASO_MAXIMO_SUBIDA = 0 se rechaza");
    debeFallar("TIMEOUT_OPERACION_CICLOS", "0", "TIMEOUT_OPERACION_CICLOS", "TIMEOUT_OPERACION_CICLOS = 0 se rechaza");
    debeFallar("COOLDOWN_CICLOS", "-1", "COOLDOWN_CICLOS", "COOLDOWN_CICLOS negativo se rechaza");
    debeFallar("INTERVALO_CICLO_SEGUNDOS", "0", "INTERVALO_CICLO_SEGUNDOS", "INTERVALO_CICLO_SEGUNDOS = 0 se rechaza");

    debeFallar("UMBRAL_SUBIDA", "abc", "UMBRAL_SUBIDA debe ser un número", "texto no numerico en un umbral: el error nombra la variable");
    debeFallar("UMBRAL_SUBIDA", "70x", "UMBRAL_SUBIDA debe ser un número", "numero con basura ('70x') se rechaza");
    debeFallar("CAPACIDAD_MAX", "cinco", "CAPACIDAD_MAX debe ser un número entero", "texto en un entero: el error nombra la variable");
    debeFallar("CAPACIDAD_MAX", "5.5", "CAPACIDAD_MAX debe ser un número entero", "decimal en un entero se rechaza");
    debeFallar("ASG_NAME", "", "ASG_NAME", "variable vacia se rechaza");
    debeFallar("PASO_MAXIMO_SUBIDA", nullptr, "PASO_MAXIMO_SUBIDA", "variable faltante sigue lanzando excepcion");

    std::cout << (fallos() ? "RESULTADO: FALLO\n" : "RESULTADO: OK\n");
    return fallos() ? 1 : 0;
}
