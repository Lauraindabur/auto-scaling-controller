#pragma once
#include <optional>
#include <string>
#include <vector>
#include "MetricSnapshot.hpp"

// Juicio sobre un snapshot antes de decidir nada. Implementa la tabla de la seccion 4.6.
struct Calidad {
    DataQuality nivel = DataQuality::INCOMPLETE;

    // Todos los motivos, tanto los que hacen el dato incompleto como las notas
    // informativas (trafico 0 interpretado, n estimado). Van al campo data_issues del log.
    std::vector<std::string> problemas;

    // RequestCount ya interpretado: el valor leido, o 0.0 cuando la ausencia se interpreta
    // como trafico cero. Es lo que se le pasa a ProactivePolicy::observar().
    std::optional<double> requestCountEfectivo;

    // n para las guardas: floor(HealthyHostCount), o InService cuando HealthyHostCount falta.
    std::optional<int> hostsSanos;

    // true si hostsSanos salio de InService y no de una medicion del ALB. Con esto se puede
    // subir, pero no bajar: reducir exige saber cuantos targets estan sanos de verdad.
    bool hostsSanosEstimado = false;

    bool completo() const { return nivel == DataQuality::COMPLETE; }
};

// Funcion pura: ni reloj ni red, asi que los tests la ejercitan con snapshots escritos a mano.
//
// tsReloj es el UNICO lugar donde entra el reloj del sistema, y entra inyectado. Hace falta
// para detectar que CloudWatch dejo de publicar: la antiguedad de un dato no se puede deducir
// de su propio dataTs. Todo lo demas (cooldowns, ventanas) se mide contra dataTs.
inline Calidad evaluarCalidad(const MetricSnapshot& s, DataTs tsReloj, int maxEdadSegundos) {
    Calidad c;
    // Lo que la fuente ya detecto (fallo de la llamada) siempre invalida el dato.
    c.problemas = s.problemas;
    bool incompleto = !s.problemas.empty();

    const auto num = [](double v) { return std::to_string(v); };

    // --- Antiguedad ---
    const DataTs edad = tsReloj - s.dataTs;
    if (edad > static_cast<DataTs>(maxEdadSegundos)) {
        c.problemas.push_back("dato vencido: " + std::to_string(edad) + " s de antiguedad, el maximo es "
                              + std::to_string(maxEdadSegundos) + " s");
        incompleto = true;
    }

    // --- CPU: obligatoria, es la metrica de estado de la parte reactiva ---
    if (!s.cpu.has_value()) {
        c.problemas.push_back("CPUUtilization ausente");
        incompleto = true;
    } else if (!(*s.cpu >= 0.0 && *s.cpu <= 100.0)) {
        c.problemas.push_back("CPUUtilization fuera de [0,100]: " + num(*s.cpu));
        incompleto = true;
    }

    // --- Estado del ASG: sin el no se sabe desde que capacidad escalar ---
    if (!s.desired.has_value() || !s.inService.has_value() || !s.pending.has_value()) {
        c.problemas.push_back("estado del ASG ausente (fallo DescribeAutoScalingGroups)");
        incompleto = true;
    } else if (*s.desired < 0 || *s.inService < 0 || *s.pending < 0) {
        c.problemas.push_back("estado del ASG con valores negativos");
        incompleto = true;
    }

    // --- HealthyHostCount ---
    bool hayHealthy = s.healthyHosts.has_value();
    if (hayHealthy && *s.healthyHosts < 0.0) {
        c.problemas.push_back("HealthyHostCount negativo: " + num(*s.healthyHosts));
        incompleto = true;
        hayHealthy = false;
    }

    // --- RequestCount ---
    if (s.requestCount.has_value() && *s.requestCount < 0.0) {
        c.problemas.push_back("RequestCount negativo: " + num(*s.requestCount));
        incompleto = true;
    } else if (s.requestCount.has_value()) {
        c.requestCountEfectivo = *s.requestCount;
    } else if (hayHealthy) {
        // El ALB no publica RequestCount cuando no hubo ninguna peticion. Que
        // HealthyHostCount si haya llegado prueba que el ALB esta publicando, asi que la
        // ausencia es trafico cero de verdad y no un fallo.
        c.requestCountEfectivo = 0.0;
        c.problemas.push_back("RequestCount ausente con HealthyHostCount presente: se interpreta como trafico 0");
    } else {
        // Sin ninguna de las dos no hay forma de distinguir "nadie pidio nada" de
        // "el ALB no esta reportando".
        c.problemas.push_back("RequestCount y HealthyHostCount ausentes: no se puede distinguir "
                              "trafico 0 de un fallo del ALB");
        incompleto = true;
    }

    // --- n para las guardas ---
    if (hayHealthy) {
        c.hostsSanos = hostsSanosEnteros(*s.healthyHosts);
    } else if (s.inService.has_value() && *s.inService >= 0) {
        c.hostsSanos = *s.inService;
        c.hostsSanosEstimado = true;
        c.problemas.push_back("HealthyHostCount ausente: se usa InService (" + std::to_string(*s.inService)
                              + ") como n; se permite subir pero no bajar");
    }

    c.nivel = incompleto ? DataQuality::INCOMPLETE : DataQuality::COMPLETE;
    return c;
}
