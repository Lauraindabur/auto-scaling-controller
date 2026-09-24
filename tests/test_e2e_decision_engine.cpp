// Pruebas end-to-end simuladas (offline): DecisionEngine real + MovingAverage,
// CooldownManager y Logger reales, con fuente de metricas y actuador falsos.
#include "DecisionEngine.hpp"
#include "Logger.hpp"
#include "test_helper.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Paso {
    bool cpuOk;
    bool rtOk;
    double cpu;
    double rt;
};
Paso ok(double cpu, double rt) { return {true, true, cpu, rt}; }
Paso falla(bool cpuOk, bool rtOk, double cpu = 0.0, double rt = 0.0) { return {cpuOk, rtOk, cpu, rt}; }

class FakeMetricSource : public IMetricSource {
public:
    explicit FakeMetricSource(std::vector<Paso> guion) : guion_(std::move(guion)) {}
    Lectura obtenerActual() override {
        if (i_ >= guion_.size()) return {false, false, false, 0.0, 0.0};
        const Paso& p = guion_[i_++];
        return {p.cpuOk && p.rtOk, p.cpuOk, p.rtOk, p.cpu, p.rt};
    }
    MetricasSecundarias obtenerMetricasSecundarias() override { return {false, 0.0}; }
private:
    std::vector<Paso> guion_;
    size_t i_ = 0;
};

class FakeActuator : public IActuator {
public:
    int capacidad = 1;
    int ciclosHastaTerminar = 1;   // llamadas a operacionTermino() que devuelven false
    bool sanas = true;
    int llamadasEjecutar = 0;
    int ultimoPaso = 0;

    int capacidadActual() override { return capacidad; }
    bool ejecutar(const std::string& decision, int paso) override {
        llamadasEjecutar++;
        ultimoPaso = paso;
        if (decision == "INCREASE_CAPACITY") capacidad += paso;
        else if (decision == "REDUCE_CAPACITY") capacidad--;
        return true;
    }
    bool instanciasRestantesSanas() override { return sanas; }
    bool operacionTermino() override {
        if (ciclosHastaTerminar > 0) { ciclosHastaTerminar--; return false; }
        return true;
    }
};

Config configBase() {
    Config cfg;
    cfg.umbralSubida = 70; cfg.umbralBajada = 30;
    cfg.umbralRtSubida = 1.0; cfg.umbralRtBajada = 1.0;
    cfg.ventanaMA = 3; cfg.cooldownCiclos = 3; cfg.timeoutOperacionCiclos = 6;
    cfg.capacidadMin = 1; cfg.capacidadMax = 5; cfg.pasoMaximoSubida = 2;
    return cfg;
}

// Extrae el valor (sin comillas) de "campo": de una linea JSON simple.
std::string campo(const std::string& linea, const std::string& nombre) {
    std::string clave = "\"" + nombre + "\":";
    auto pos = linea.find(clave);
    if (pos == std::string::npos) return "?";
    pos += clave.size();
    bool comillas = linea[pos] == '"';
    if (comillas) pos++;
    auto fin = linea.find(comillas ? '"' : ',', pos);
    if (fin == std::string::npos) fin = linea.find('}', pos);
    return linea.substr(pos, fin - pos);
}

std::vector<std::string> leerLog(const char* ruta) {
    std::ifstream in(ruta);
    std::vector<std::string> lineas;
    for (std::string l; std::getline(in, l);) lineas.push_back(l);
    return lineas;
}

void tabla(const std::vector<std::string>& lineas) {
    std::cout << "ciclo | cpu   | MA_CPU | rt    | MA_RT | cap | decision           | trigger | estado_op   | cd | justificacion\n";
    for (size_t i = 0; i < lineas.size(); i++) {
        const auto& l = lineas[i];
        std::cout << (i + 1) << "\t| " << campo(l, "cpu_utilization") << "\t| " << campo(l, "moving_average_cpu")
                  << "\t| " << campo(l, "target_response_time") << "\t| " << campo(l, "moving_average_response_time")
                  << "\t| " << campo(l, "current_capacity") << "\t| " << campo(l, "decision")
                  << "\t| " << campo(l, "decision_trigger") << "\t| " << campo(l, "operation_state")
                  << "\t| " << campo(l, "cooldown_remaining") << "\t| " << campo(l, "justification") << "\n";
    }
    std::cout << "\n";
}

struct Esperado {
    const char* decision;
    const char* trigger;
    const char* justificacion;
    const char* estadoOp;
    const char* cooldown;
    const char* etiqueta;
};

bool coincide(const std::string& l, const Esperado& e) {
    bool ok = campo(l, "decision") == e.decision &&
              campo(l, "decision_trigger") == e.trigger &&
              campo(l, "justification") == e.justificacion &&
              campo(l, "operation_state") == e.estadoOp &&
              campo(l, "cooldown_remaining") == e.cooldown;
    if (!ok) {
        std::cout << "  esperado: " << e.decision << " | " << e.trigger << " | " << e.justificacion
                  << " | " << e.estadoOp << " | cooldown=" << e.cooldown << "\n"
                  << "  obtenido: " << campo(l, "decision") << " | " << campo(l, "decision_trigger")
                  << " | " << campo(l, "justification") << " | " << campo(l, "operation_state")
                  << " | cooldown=" << campo(l, "cooldown_remaining") << "\n";
    }
    return ok;
}

const char* RUTA = "test_e2e_decisions.jsonl";

// --- Escenario 1: secuencia completa de transiciones (CPU manda, RT bajo y estable) ---
void escenarioSecuencia() {
    std::cout << "\n=== Escenario 1: secuencia de transiciones ===\n";
    std::remove(RUTA);
    Config cfg = configBase();
    std::vector<double> cpu = {80, 80, 80, 80, 80, 60, 60, 50, 50, 5, 5};
    std::vector<Paso> guion;
    for (double c : cpu) guion.push_back(ok(c, 0.2));
    FakeMetricSource metrics(guion);
    FakeActuator actuator;
    MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
    CooldownManager cooldown(cfg.cooldownCiclos);

    const std::vector<Esperado> esperado = {
        {"MAINTAIN_CAPACITY", "NONE",   "historial insuficiente: MA_CPU y MA_RT",            "NONE",        "0", "1. historial insuficiente (1 muestra)"},
        {"MAINTAIN_CAPACITY", "NONE",   "historial insuficiente: MA_CPU y MA_RT",            "NONE",        "0", "1. historial insuficiente (2 muestras)"},
        {"INCREASE_CAPACITY", "CPU",    "MA_CPU por encima del umbral de subida; capacidad objetivo 2, paso +1", "IN_PROGRESS", "0", "2. MA_CPU > 70 -> sube capacidad"},
        {"MAINTAIN_CAPACITY", "NONE",   "operación en curso",                                "IN_PROGRESS", "0", "3. operacion IN_PROGRESS"},
        {"MAINTAIN_CAPACITY", "NONE",   "operación confirmada, inicia cooldown",             "NONE",        "3", "3b. operacion confirmada (arranca cooldown)"},
        {"MAINTAIN_CAPACITY", "NONE",   "en periodo de cooldown",                            "NONE",        "2", "4. cooldown ciclo 1"},
        {"MAINTAIN_CAPACITY", "NONE",   "en periodo de cooldown",                            "NONE",        "1", "4. cooldown ciclo 2"},
        {"MAINTAIN_CAPACITY", "NONE",   "en periodo de cooldown",                            "NONE",        "0", "4. cooldown ciclo 3"},
        {"MAINTAIN_CAPACITY", "NONE",   "dentro del rango esperado",                         "NONE",        "0", "5. evaluacion normal tras cooldown"},
        {"MAINTAIN_CAPACITY", "NONE",   "dentro del rango esperado",                         "NONE",        "0", "5. evaluacion normal (MA_CPU 35)"},
        {"REDUCE_CAPACITY",   "CPU+RT", "MA_CPU y MA_RT por debajo de sus umbrales de bajada", "IN_PROGRESS", "0", "6. CPU y RT bajos -> baja capacidad"},
    };
    {
        Logger logger(RUTA);
        DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
        for (size_t i = 0; i < esperado.size(); i++) engine.ejecutarCiclo();
    }
    auto lineas = leerLog(RUTA);
    tabla(lineas);
    CHECK(lineas.size() == esperado.size(), "se registro una linea por ciclo (11)");
    for (size_t i = 0; i < lineas.size() && i < esperado.size(); i++)
        CHECK(coincide(lineas[i], esperado[i]), "E1 ciclo " << (i + 1) << " - " << esperado[i].etiqueta);
    CHECK(actuator.llamadasEjecutar == 2, "E1: el actuador recibio exactamente 2 ordenes");
    CHECK(actuator.capacidad == 1, "E1: capacidad final = 1 (1 -> 2 -> 1)");
    std::remove(RUTA);
}

// --- Escenario 2: politica OR (subir) / AND (bajar) y justificaciones por disparador ---
struct CasoPolitica {
    const char* nombre;
    double cpu, rt;
    int capacidad;
    bool sanas;
    Esperado esp;
};

void escenarioPolitica() {
    std::cout << "\n=== Escenario 2: politica OR/AND (decision en el 3er ciclo, 3 muestras iguales) ===\n";
    const std::vector<CasoPolitica> casos = {
        {"SUBIR solo por CPU",            80, 0.2, 1, true,  {"INCREASE_CAPACITY", "CPU",    "MA_CPU por encima del umbral de subida; capacidad objetivo 2, paso +1",                 "IN_PROGRESS", "0", ""}},
        {"SUBIR solo por RT",             50, 1.5, 1, true,  {"INCREASE_CAPACITY", "RT",     "MA_RT por encima del umbral de subida; capacidad objetivo 2, paso +1",                  "IN_PROGRESS", "0", ""}},
        {"SUBIR por CPU y RT",            80, 1.5, 1, true,  {"INCREASE_CAPACITY", "CPU+RT", "MA_CPU y MA_RT por encima de sus umbrales de subida; capacidad objetivo 2, paso +1", "IN_PROGRESS", "0", ""}},
        {"CPU baja + RT alta -> SUBE (OR gana, no baja)", 10, 1.5, 3, true, {"INCREASE_CAPACITY", "RT", "MA_RT por encima del umbral de subida; capacidad objetivo 5, paso +2", "IN_PROGRESS", "0", ""}},
        {"BAJAR: CPU baja y RT baja",     10, 0.2, 3, true,  {"REDUCE_CAPACITY",   "CPU+RT", "MA_CPU y MA_RT por debajo de sus umbrales de bajada",   "IN_PROGRESS", "0", ""}},
        {"BAJAR con RT 0.0 (sin trafico)", 10, 0.0, 3, true, {"REDUCE_CAPACITY",   "CPU+RT", "MA_CPU y MA_RT por debajo de sus umbrales de bajada",   "IN_PROGRESS", "0", ""}},
        {"CPU baja + RT justo en 1.0 -> mantiene", 10, 1.0, 3, true, {"MAINTAIN_CAPACITY", "NONE", "MA_CPU baja pero MA_RT no está por debajo del umbral de bajada", "NONE", "0", ""}},
        {"CPU media + RT baja -> mantiene", 50, 0.2, 3, true, {"MAINTAIN_CAPACITY", "NONE", "dentro del rango esperado",                                  "NONE",        "0", ""}},
        {"CPU=70 y RT=1.0 exactos -> mantiene (umbrales estrictos)", 70, 1.0, 3, true, {"MAINTAIN_CAPACITY", "NONE", "dentro del rango esperado",     "NONE",        "0", ""}},
        {"SUBIR por RT pero en capacidad maxima", 50, 1.5, 5, true, {"MAINTAIN_CAPACITY", "RT", "límite máximo alcanzado (MA_RT por encima del umbral de subida)", "NONE", "0", ""}},
        {"BAJAR pero en capacidad minima", 10, 0.2, 1, true, {"MAINTAIN_CAPACITY", "CPU+RT", "límite mínimo alcanzado (MA_CPU y MA_RT por debajo de sus umbrales de bajada)", "NONE", "0", ""}},
        {"BAJAR pero instancias no sanas", 10, 0.2, 3, false, {"MAINTAIN_CAPACITY", "CPU+RT", "no seguro reducir",                                       "NONE",        "0", ""}},
    };

    for (const auto& c : casos) {
        std::remove(RUTA);
        Config cfg = configBase();
        FakeMetricSource metrics({ok(c.cpu, c.rt), ok(c.cpu, c.rt), ok(c.cpu, c.rt)});
        FakeActuator actuator;
        actuator.capacidad = c.capacidad;
        actuator.sanas = c.sanas;
        MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
        CooldownManager cooldown(cfg.cooldownCiclos);
        {
            Logger logger(RUTA);
            DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
            for (int i = 0; i < 3; i++) engine.ejecutarCiclo();
        }
        auto lineas = leerLog(RUTA);
        std::cout << "-- " << c.nombre << " (cpu=" << c.cpu << ", rt=" << c.rt << ", capacidad=" << c.capacidad << ")\n";
        if (lineas.size() == 3) {
            const auto& l = lineas[2];
            std::cout << "   " << campo(l, "decision") << " | trigger=" << campo(l, "decision_trigger")
                      << " | MA_CPU=" << campo(l, "moving_average_cpu") << " MA_RT=" << campo(l, "moving_average_response_time")
                      << " | " << campo(l, "justification") << "\n";
        }
        CHECK(lineas.size() == 3 && coincide(lineas[2], c.esp), std::string("E2 ") + c.nombre);
    }
    std::remove(RUTA);
}

// --- Escenario 3: fallo parcial de metrica (regla 1) ---
void escenarioFalloParcial() {
    std::cout << "\n=== Escenario 3: fallo parcial de metrica ===\n";
    std::remove(RUTA);
    Config cfg = configBase();
    // Los valores de los ciclos fallidos son basura a proposito: si algun buffer se
    // actualizara en un ciclo fallido, las MA finales no serian 80 / 0.2.
    FakeMetricSource metrics({
        ok(80, 0.2),
        ok(80, 0.2),
        falla(true,  false, 999, 999),   // falla RT (CPU llego bien)
        falla(false, true,  999, 999),   // falla CPU (RT llego bien)
        falla(false, false, 999, 999),   // fallan ambas
        ok(80, 0.2),
    });
    FakeActuator actuator;
    MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
    CooldownManager cooldown(cfg.cooldownCiclos);
    {
        Logger logger(RUTA);
        DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
        for (int i = 0; i < 5; i++) engine.ejecutarCiclo();
        CHECK(maCpu.muestrasActuales() == 2, "E3: tras 3 ciclos fallidos MA_CPU sigue con 2 muestras");
        CHECK(maRt.muestrasActuales() == 2, "E3: tras 3 ciclos fallidos MA_RT sigue con 2 muestras");
        engine.ejecutarCiclo();
    }
    auto lineas = leerLog(RUTA);
    tabla(lineas);
    const std::vector<Esperado> esperado = {
        {"MAINTAIN_CAPACITY", "NONE", "historial insuficiente: MA_CPU y MA_RT",                   "NONE", "0", "historial 1"},
        {"MAINTAIN_CAPACITY", "NONE", "historial insuficiente: MA_CPU y MA_RT",                   "NONE", "0", "historial 2"},
        {"MAINTAIN_CAPACITY", "NONE", "TargetResponseTime no disponible tras agotar reintentos",  "NONE", "0", "falla solo RT -> ciclo fallido"},
        {"MAINTAIN_CAPACITY", "NONE", "CPUUtilization no disponible tras agotar reintentos",      "NONE", "0", "falla solo CPU -> ciclo fallido"},
        {"MAINTAIN_CAPACITY", "NONE", "CPUUtilization y TargetResponseTime no disponibles tras agotar reintentos", "NONE", "0", "fallan ambas"},
        {"INCREASE_CAPACITY", "CPU",  "MA_CPU por encima del umbral de subida; capacidad objetivo 2, paso +1", "IN_PROGRESS", "0", "3a muestra valida completa el historial"},
    };
    CHECK(lineas.size() == esperado.size(), "E3: una linea por ciclo (6)");
    for (size_t i = 0; i < lineas.size() && i < esperado.size(); i++)
        CHECK(coincide(lineas[i], esperado[i]), "E3 ciclo " << (i + 1) << " - " << esperado[i].etiqueta);
    if (lineas.size() == 6) {
        CHECK_NEAR(std::stod(campo(lineas[5], "moving_average_cpu")), 80.0, 1e-6, "E3: MA_CPU final = 80 (sin basura de ciclos fallidos)");
        CHECK_NEAR(std::stod(campo(lineas[5], "moving_average_response_time")), 0.2, 1e-6, "E3: MA_RT final = 0.2 (sin basura de ciclos fallidos)");
        CHECK(campo(lineas[2], "cpu_utilization") == "null", "E3: ciclo fallido no registra CPU cruda");
    }
    std::remove(RUTA);
}

// --- Escenario 4: arranque en frio con una sola MA completa (regla 2) ---
void escenarioArranqueEnFrio() {
    std::cout << "\n=== Escenario 4: arranque en frio, MA_CPU completa y MA_RT con 1 muestra ===\n";
    std::remove(RUTA);
    Config cfg = configBase();
    MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
    maCpu.prellenar({50, 50, 50});   // prefill completo
    maRt.prellenar({0.2});           // prefill parcial
    FakeMetricSource metrics({ok(50, 0.2), ok(50, 0.2), ok(50, 0.2)});
    FakeActuator actuator;
    CooldownManager cooldown(cfg.cooldownCiclos);
    {
        Logger logger(RUTA);
        DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
        for (int i = 0; i < 3; i++) engine.ejecutarCiclo();
    }
    auto lineas = leerLog(RUTA);
    tabla(lineas);
    const std::vector<Esperado> esperado = {
        {"MAINTAIN_CAPACITY", "NONE", "historial insuficiente: MA_RT",  "NONE", "0", "MA_CPU completa pero MA_RT con 2 muestras -> espera"},
        {"MAINTAIN_CAPACITY", "NONE", "dentro del rango esperado",      "NONE", "0", "ambas completas -> evalua (MA_RT llego a 3)"},
        {"MAINTAIN_CAPACITY", "NONE", "dentro del rango esperado",      "NONE", "0", "sigue evaluando normal"},
    };
    CHECK(lineas.size() == esperado.size(), "E4: una linea por ciclo (3)");
    for (size_t i = 0; i < lineas.size() && i < esperado.size(); i++)
        CHECK(coincide(lineas[i], esperado[i]), "E4 ciclo " << (i + 1) << " - " << esperado[i].etiqueta);
    std::remove(RUTA);
}

// --- Escenario 5: timeout de operacion en curso (TIMEOUT_OPERACION_CICLOS = 6) ---
// Metricas constantes (CPU 80, RT 0.2): el ciclo 3 sube capacidad y queda IN_PROGRESS.
// ciclosHastaTerminar = cuantas veces operacionTermino() responde false antes de responder true.
std::vector<std::string> correrOperacion(int ciclosHastaTerminar, int nCiclos, FakeActuator& actuator) {
    std::remove(RUTA);
    Config cfg = configBase();
    std::vector<Paso> guion(nCiclos, ok(80, 0.2));
    FakeMetricSource metrics(guion);
    actuator.ciclosHastaTerminar = ciclosHastaTerminar;
    MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
    CooldownManager cooldown(cfg.cooldownCiclos);
    {
        Logger logger(RUTA);
        DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
        for (int i = 0; i < nCiclos; i++) engine.ejecutarCiclo();
    }
    auto lineas = leerLog(RUTA);
    std::remove(RUTA);
    return lineas;
}

void escenarioTimeoutOperacion() {
    const Esperado hist = {"MAINTAIN_CAPACITY", "NONE", "historial insuficiente: MA_CPU y MA_RT", "NONE", "0", "historial insuficiente"};
    const Esperado sube = {"INCREASE_CAPACITY", "CPU", "MA_CPU por encima del umbral de subida; capacidad objetivo 2, paso +1", "IN_PROGRESS", "0", "sube capacidad"};
    const Esperado curso = {"MAINTAIN_CAPACITY", "NONE", "operación en curso", "IN_PROGRESS", "0", "operacion en curso"};

    // 5a: se confirma en el ultimo ciclo permitido (contador = 6): comportamiento normal.
    {
        std::cout << "\n=== Escenario 5a: operacion confirmada con el contador en 6 (no hay timeout) ===\n";
        FakeActuator actuator;
        auto lineas = correrOperacion(5, 10, actuator);
        tabla(lineas);
        const Esperado conf = {"MAINTAIN_CAPACITY", "NONE", "operación confirmada, inicia cooldown", "NONE", "3", "confirmada"};
        const Esperado cd2 = {"MAINTAIN_CAPACITY", "NONE", "en periodo de cooldown", "NONE", "2", "cooldown"};
        std::vector<Esperado> esp = {hist, hist, sube, curso, curso, curso, curso, curso, conf, cd2};
        CHECK(lineas.size() == esp.size(), "E5a: una linea por ciclo (10)");
        for (size_t i = 0; i < lineas.size() && i < esp.size(); i++)
            CHECK(coincide(lineas[i], esp[i]), "E5a ciclo " << (i + 1) << " - " << esp[i].etiqueta);
    }

    // 5b: se confirma justo en el ciclo 7 (contador = 7 > 6): la confirmacion se revisa antes que el timeout.
    {
        std::cout << "\n=== Escenario 5b: confirmada en el ciclo en que el contador llega a 7 (gana la confirmacion) ===\n";
        FakeActuator actuator;
        auto lineas = correrOperacion(6, 10, actuator);
        tabla(lineas);
        const Esperado conf = {"MAINTAIN_CAPACITY", "NONE", "operación confirmada, inicia cooldown", "NONE", "3", "confirmada"};
        std::vector<Esperado> esp = {hist, hist, sube, curso, curso, curso, curso, curso, curso, conf};
        CHECK(lineas.size() == esp.size(), "E5b: una linea por ciclo (10)");
        for (size_t i = 0; i < lineas.size() && i < esp.size(); i++)
            CHECK(coincide(lineas[i], esp[i]), "E5b ciclo " << (i + 1) << " - " << esp[i].etiqueta);
    }

    // 5c: nunca se confirma -> timeout, cooldown, y luego vuelve a evaluar por sus metricas.
    {
        std::cout << "\n=== Escenario 5c: la operacion nunca se confirma -> timeout, cooldown, evaluacion normal ===\n";
        FakeActuator actuator;
        auto lineas = correrOperacion(1000, 14, actuator);
        tabla(lineas);
        const Esperado tout = {"MAINTAIN_CAPACITY", "NONE", "operación excedió el tiempo máximo (6 ciclos), se marca como fallida", "FAILED", "3", "timeout: se marca fallida, arranca cooldown"};
        const Esperado cd2 = {"MAINTAIN_CAPACITY", "NONE", "en periodo de cooldown", "NONE", "2", "cooldown ciclo 1"};
        const Esperado cd1 = {"MAINTAIN_CAPACITY", "NONE", "en periodo de cooldown", "NONE", "1", "cooldown ciclo 2"};
        const Esperado cd0 = {"MAINTAIN_CAPACITY", "NONE", "en periodo de cooldown", "NONE", "0", "cooldown ciclo 3"};
        const Esperado otra = {"INCREASE_CAPACITY", "CPU", "MA_CPU por encima del umbral de subida; capacidad objetivo 3, paso +1", "IN_PROGRESS", "0", "vuelve a evaluar y decide subir por las metricas"};
        std::vector<Esperado> esp = {hist, hist, sube, curso, curso, curso, curso, curso, curso, tout, cd2, cd1, cd0, otra};
        CHECK(lineas.size() == esp.size(), "E5c: una linea por ciclo (14)");
        for (size_t i = 0; i < lineas.size() && i < esp.size(); i++)
            CHECK(coincide(lineas[i], esp[i]), "E5c ciclo " << (i + 1) << " - " << esp[i].etiqueta);
        if (lineas.size() == 14) {
            CHECK(campo(lineas[9], "action_result") == "FAILED", "E5c: el ciclo del timeout registra action_result = FAILED");
            CHECK(campo(lineas[9], "requested_action") == "NONE", "E5c: el timeout no pide ninguna accion (no deshace nada)");
            CHECK(campo(lineas[13], "current_capacity") == "2", "E5c: la capacidad no se toco durante el timeout (sigue en 2)");
        }
        CHECK(actuator.llamadasEjecutar == 2, "E5c: solo 2 ordenes al actuador (la original y la nueva decidida por metricas)");
    }
}

// --- Escenario 6: subida proporcional (paso calculado a partir de MA_RT) ---
// N_necesarias = techo(N_actual * MA_RT / 1.0); tope = CAPACIDAD_MAX (5); paso = min(N_necesarias - N_actual, 2); minimo +1.
struct CasoPaso {
    const char* nombre;
    double cpu, rt;
    int capacidad;
    const char* trigger;
    const char* justificacion;
    const char* accionSolicitada;
    int pasoEsperado;
    int capacidadFinal;
};

void escenarioPasoProporcional() {
    std::cout << "\n=== Escenario 6: subida proporcional (decision en el 3er ciclo, 3 muestras iguales) ===\n";
    const std::vector<CasoPaso> casos = {
        {"regresion corrida real: cap 1, MA_RT 1.319 -> techo(1.319)=2 -> +1", 50, 1.319, 1, "RT",
         "MA_RT por encima del umbral de subida; capacidad objetivo 2, paso +1", "INCREASE_CAPACITY -> 2", 1, 2},
        {"exceso grande: cap 1, MA_RT 2.5 -> techo(2.5)=3 -> +2", 50, 2.5, 1, "RT",
         "MA_RT por encima del umbral de subida; capacidad objetivo 3, paso +2", "INCREASE_CAPACITY -> 3", 2, 3},
        {"tope de paso: cap 1, MA_RT 4.0 -> pide 4 (+3) -> limitado a +2", 50, 4.0, 1, "RT",
         "MA_RT por encima del umbral de subida; capacidad objetivo 3, paso +2", "INCREASE_CAPACITY -> 3", 2, 3},
        {"tope de paso: cap 2, MA_RT 1.6 -> techo(3.2)=4 (+2) -> +2 (justo en el tope)", 50, 1.6, 2, "RT",
         "MA_RT por encima del umbral de subida; capacidad objetivo 4, paso +2", "INCREASE_CAPACITY -> 4", 2, 4},
        {"tope de capacidad: cap 4, MA_RT 3.0 -> pide 12, tope 5 -> solo +1", 50, 3.0, 4, "RT",
         "MA_RT por encima del umbral de subida; capacidad objetivo 5, paso +1", "INCREASE_CAPACITY -> 5", 1, 5},
        {"tope de capacidad: cap 3, MA_RT 3.0 -> pide 9, tope 5 (+2) -> +2", 50, 3.0, 3, "RT",
         "MA_RT por encima del umbral de subida; capacidad objetivo 5, paso +2", "INCREASE_CAPACITY -> 5", 2, 5},
        {"solo CPU: cap 3, MA_CPU 90, MA_RT 0.5 -> +1 (sin modelo para CPU)", 90, 0.5, 3, "CPU",
         "MA_CPU por encima del umbral de subida; capacidad objetivo 4, paso +1", "INCREASE_CAPACITY -> 4", 1, 4},
        {"CPU y RT: cap 1, MA_CPU 90, MA_RT 2.5 -> formula por RT -> +2", 90, 2.5, 1, "CPU+RT",
         "MA_CPU y MA_RT por encima de sus umbrales de subida; capacidad objetivo 3, paso +2", "INCREASE_CAPACITY -> 3", 2, 3},
    };

    for (const auto& c : casos) {
        std::remove(RUTA);
        Config cfg = configBase();
        FakeMetricSource metrics({ok(c.cpu, c.rt), ok(c.cpu, c.rt), ok(c.cpu, c.rt)});
        FakeActuator actuator;
        actuator.capacidad = c.capacidad;
        MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
        CooldownManager cooldown(cfg.cooldownCiclos);
        {
            Logger logger(RUTA);
            DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
            for (int i = 0; i < 3; i++) engine.ejecutarCiclo();
        }
        auto lineas = leerLog(RUTA);
        std::cout << "-- " << c.nombre << "\n";
        bool ok3 = lineas.size() == 3;
        if (ok3) {
            const auto& l = lineas[2];
            std::cout << "   " << campo(l, "decision") << " | trigger=" << campo(l, "decision_trigger")
                      << " | MA_RT=" << campo(l, "moving_average_response_time")
                      << " | requested_action=" << campo(l, "requested_action")
                      << " | " << campo(l, "justification") << "\n";
            ok3 = campo(l, "decision") == "INCREASE_CAPACITY" &&
                  campo(l, "decision_trigger") == c.trigger &&
                  campo(l, "justification") == c.justificacion &&
                  campo(l, "requested_action") == c.accionSolicitada &&
                  campo(l, "operation_state") == "IN_PROGRESS";
        }
        CHECK(ok3, std::string("E6 log: ") + c.nombre);
        CHECK(actuator.ultimoPaso == c.pasoEsperado, std::string("E6 paso pedido al actuador = ") + std::to_string(c.pasoEsperado) + " (" + c.nombre + ")");
        CHECK(actuator.capacidad == c.capacidadFinal && actuator.capacidad <= cfg.capacidadMax,
              std::string("E6 capacidad resultante = ") + std::to_string(c.capacidadFinal) + " y nunca > 5");
    }

    // La bajada no cambia: sigue siendo -1.
    {
        std::remove(RUTA);
        Config cfg = configBase();
        FakeMetricSource metrics({ok(10, 0.2), ok(10, 0.2), ok(10, 0.2)});
        FakeActuator actuator;
        actuator.capacidad = 4;
        MovingAverage maCpu(cfg.ventanaMA), maRt(cfg.ventanaMA);
        CooldownManager cooldown(cfg.cooldownCiclos);
        {
            Logger logger(RUTA);
            DecisionEngine engine(cfg, metrics, maCpu, maRt, cooldown, actuator, logger);
            for (int i = 0; i < 3; i++) engine.ejecutarCiclo();
        }
        auto lineas = leerLog(RUTA);
        std::cout << "-- bajada: cap 4, CPU y RT bajos -> -1 (no cambia)\n";
        CHECK(lineas.size() == 3 && campo(lineas[2], "decision") == "REDUCE_CAPACITY" &&
              campo(lineas[2], "requested_action") == "REDUCE_CAPACITY -> 3", "E6 bajada sigue siendo de a -1 (requested_action REDUCE_CAPACITY -> 3)");
        CHECK(actuator.capacidad == 3, "E6 capacidad tras bajar = 3");
    }
    std::remove(RUTA);
}

} // namespace

int main() {
    escenarioSecuencia();
    escenarioPolitica();
    escenarioFalloParcial();
    escenarioArranqueEnFrio();
    escenarioTimeoutOperacion();
    escenarioPasoProporcional();
    std::cout << (fallos() ? "\nRESULTADO: FALLO\n" : "\nRESULTADO: OK\n");
    return fallos() ? 1 : 0;
}
