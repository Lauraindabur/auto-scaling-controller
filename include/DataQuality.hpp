#pragma once
#include <optional>
#include <string>
#include <vector>
#include "MetricSnapshot.hpp"

using namespace std;

// Analiza la calidad de un snapshot antes de decidir nada. 
struct Calidad {
    DataQuality nivel = DataQuality::INCOMPLETE;

    vector<string> problemas;
    optional<double> requestCountEfectivo;
    optional<int> hostsSanos;

    // true si hostsSanos salio de InService y no de una medicion del ALB. Con esto se puede
    // subir, pero no bajar-> reducir tiene que saber cuantos targets estan sanos de verdad.
    bool hostsSanosEstimado = false;

    bool completo() const { return nivel == DataQuality::COMPLETE; }
};


// tsReloj -> para detectar que CloudWatch dejo de publicar: la antiguedad de un dato no se puede deducir
// de su dataTs -> timestamp del minuto que se esta procesando
inline Calidad evaluarCalidad(const MetricSnapshot& s, DataTs tsReloj, int maxEdadSegundos) {
    Calidad c;
    c.problemas = s.problemas;
    bool incompleto = !s.problemas.empty();

    const auto num = [](double v) { return to_string(v); }; // para meter un double en un mensaje

    const DataTs edad = tsReloj - s.dataTs;
    if (edad > static_cast<DataTs>(maxEdadSegundos)) {
        c.problemas.push_back("dato vencido: " + to_string(edad) + " s de antiguedad, el maximo es "
                              + to_string(maxEdadSegundos) + " s");
        incompleto = true;
    }

    if (!s.cpu.has_value()) {
        c.problemas.push_back("CPUUtilization ausente");
        incompleto = true;
    } else if (!(*s.cpu >= 0.0 && *s.cpu <= 100.0)) {
        c.problemas.push_back("CPUUtilization fuera de [0,100]: " + num(*s.cpu));
        incompleto = true;
    }

    if (!s.desired.has_value() || !s.inService.has_value() || !s.pending.has_value()) {
        c.problemas.push_back("estado del ASG ausente (fallo DescribeAutoScalingGroups)");
        incompleto = true;
    } else if (*s.desired < 0 || *s.inService < 0 || *s.pending < 0) {
        c.problemas.push_back("estado del ASG con valores negativos");
        incompleto = true;
    }

    bool hayHealthy = s.healthyHosts.has_value();
    if (hayHealthy && *s.healthyHosts < 0.0) {
        c.problemas.push_back("HealthyHostCount negativo: " + num(*s.healthyHosts));
        incompleto = true;
        hayHealthy = false;
    }

    if (s.requestCount.has_value() && *s.requestCount < 0.0) {
        c.problemas.push_back("RequestCount negativo: " + num(*s.requestCount));
        incompleto = true;
    } else if (s.requestCount.has_value()) {
        c.requestCountEfectivo = *s.requestCount;
    } else if (hayHealthy) {
        // que HealthyHostCount si haya llegado prueba que el ALB esta publicando, ausnecia de trafico -> 0 y no un fallo 
        c.requestCountEfectivo = 0.0;
        c.problemas.push_back("RequestCount ausente con HealthyHostCount presente: se interpreta como trafico 0");
    } else {
        c.problemas.push_back("RequestCount y HealthyHostCount ausentes: no se puede distinguir "
                              "trafico 0 de un fallo del ALB");
        incompleto = true;
    }

    if (hayHealthy) {
        c.hostsSanos = hostsSanosEnteros(*s.healthyHosts);
    } else if (s.inService.has_value() && *s.inService >= 0) {
        c.hostsSanos = *s.inService;
        c.hostsSanosEstimado = true;
        c.problemas.push_back("HealthyHostCount ausente: se usa InService (" + to_string(*s.inService)
                              + ") como n; se permite subir pero no bajar");
    }

    c.nivel = incompleto ? DataQuality::INCOMPLETE : DataQuality::COMPLETE;
    return c;
}
