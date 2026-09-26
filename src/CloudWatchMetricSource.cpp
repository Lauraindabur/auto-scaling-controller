#include "CloudWatchMetricSource.hpp"
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>
#include <aws/autoscaling/model/DescribeAutoScalingGroupsRequest.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/core/utils/DateTime.h>
#include <aws/monitoring/model/Dimension.h>
#include <aws/monitoring/model/GetMetricDataRequest.h>
#include <aws/monitoring/model/Metric.h>
#include <aws/monitoring/model/MetricDataQuery.h>
#include <aws/monitoring/model/MetricStat.h>

using namespace std;

namespace {

// Cuanto hacia atras se busca el ultimo periodo completo. Generoso a proposito (muy por
// encima del periodo de 60s): CloudWatch a veces tarda mas de un minuto en publicar, y un
// hueco de un par de ciclos no debe hacer que la fuente reporte "sin dato" de mas.
const chrono::minutes VENTANA_BUSQUEDA{10};

Aws::CloudWatch::Model::MetricDataQuery construirQuery(
    const string& id, const string& ns, const string& metricName,
    const vector<pair<string, string>>& dimensiones, const string& estadistico,
    int periodoSegundos) {
    Aws::CloudWatch::Model::Metric metric;
    metric.SetNamespace(ns);
    metric.SetMetricName(metricName);
    for (const auto& d : dimensiones) {
        Aws::CloudWatch::Model::Dimension dim;
        dim.SetName(d.first);
        dim.SetValue(d.second);
        metric.AddDimensions(dim);
    }

    Aws::CloudWatch::Model::MetricStat stat;
    stat.SetMetric(metric);
    stat.SetPeriod(periodoSegundos);
    stat.SetStat(estadistico);

    Aws::CloudWatch::Model::MetricDataQuery query;
    query.SetId(id);
    query.SetMetricStat(stat);
    query.SetReturnData(true);
    return query;
}

// Un punto (inicio de periodo, valor) cuyo periodo ya cerro del todo: timestamp + periodo
// <= ahora. Se descarta el minuto en curso (seccion 4.2), que CloudWatch a veces devuelve
// ya con un valor parcial.
struct Punto { DataTs inicio; double valor; };

vector<Punto> puntosCompletos(const Aws::CloudWatch::Model::MetricDataResult& r,
                              DataTs ahoraEpoch, int periodoSegundos) {
    vector<Punto> puntos;
    const auto& timestamps = r.GetTimestamps();
    const auto& valores = r.GetValues();
    const size_t n = min(timestamps.size(), valores.size());
    for (size_t i = 0; i < n; i++) {
        const DataTs inicio = static_cast<DataTs>(timestamps[i].Seconds());
        if (inicio + periodoSegundos <= ahoraEpoch) {
            puntos.push_back({inicio, valores[i]});
        }
    }
    sort(puntos.begin(), puntos.end(), [](const Punto& a, const Punto& b) { return a.inicio < b.inicio; });
    return puntos;
}

optional<double> valorEnPeriodo(const vector<Punto>& puntos, DataTs inicio) {
    for (auto it = puntos.rbegin(); it != puntos.rend(); ++it) {
        if (it->inicio == inicio) return it->valor;
    }
    return nullopt;
}

// RequestCount (Sum) a veces todavia no esta publicado en el minuto del ciclo actual
// cuando se consulta (retraso de publicacion de CloudWatch, confirmado con datos reales:
// el mismo minuto SI tenia dato unos minutos despues). Si falta justo en dataTs, se usa
// el minuto anterior si ya llego; si tampoco esta, sigue ausente igual que antes. No
// cambia el ciclo de decision (sigue en dataTs) ni las queries de CPU/HealthyHostCount.
optional<double> valorRequestCount(const vector<Punto>& puntos, DataTs dataTs, int periodoSegundos) {
    if (auto v = valorEnPeriodo(puntos, dataTs)) return v;
    return valorEnPeriodo(puntos, dataTs - periodoSegundos);
}

const Aws::CloudWatch::Model::MetricDataResult* buscarResultado(
    const vector<Aws::CloudWatch::Model::MetricDataResult>& resultados, const string& id) {
    for (const auto& r : resultados) {
        if (r.GetId() == id) return &r;
    }
    return nullptr;
}

} // namespace

CloudWatchMetricSource::CloudWatchMetricSource(string asgName, string loadBalancerDim,
                                               string targetGroupDim, const string& region,
                                               int periodoSegundos)
    : asgName_(move(asgName)), loadBalancerDim_(move(loadBalancerDim)),
      targetGroupDim_(move(targetGroupDim)), periodoSegundos_(periodoSegundos) {
    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.retryStrategy = Aws::MakeShared<Aws::Client::DefaultRetryStrategy>("CloudWatchMetricSource", 3);
    cwClient_ = Aws::CloudWatch::CloudWatchClient(config);
    asClient_ = Aws::AutoScaling::AutoScalingClient(config);
}

Aws::CloudWatch::Model::GetMetricDataRequest CloudWatchMetricSource::construirRequest(
    const Aws::Utils::DateTime& ahora) const {
    Aws::CloudWatch::Model::GetMetricDataRequest req;
    req.AddMetricDataQueries(construirQuery("cpu", "AWS/EC2", "CPUUtilization",
        {{"AutoScalingGroupName", asgName_}}, "Average", periodoSegundos_));
    req.AddMetricDataQueries(construirQuery("rc", "AWS/ApplicationELB", "RequestCount",
        {{"LoadBalancer", loadBalancerDim_}}, "Sum", periodoSegundos_));
    req.AddMetricDataQueries(construirQuery("hh", "AWS/ApplicationELB", "HealthyHostCount",
        {{"LoadBalancer", loadBalancerDim_}, {"TargetGroup", targetGroupDim_}}, "Average", periodoSegundos_));
    req.SetStartTime(ahora - VENTANA_BUSQUEDA);
    req.SetEndTime(ahora);
    return req;
}

optional<MetricSnapshot> CloudWatchMetricSource::ultimoPeriodoCompleto() {
    const auto ahora = Aws::Utils::DateTime::Now();
    const DataTs ahoraEpoch = static_cast<DataTs>(ahora.Seconds());

    const auto outcome = cwClient_.GetMetricData(construirRequest(ahora));
    if (!outcome.IsSuccess()) {
        // Fallo total de la llamada: no hay ni timestamp con el que registrar un ciclo.
        return nullopt;
    }

    const auto& resultados = outcome.GetResult().GetMetricDataResults();
    const auto* rCpu = buscarResultado(resultados, "cpu");
    const auto* rRc = buscarResultado(resultados, "rc");
    const auto* rHh = buscarResultado(resultados, "hh");

    const auto puntosCpu = rCpu ? puntosCompletos(*rCpu, ahoraEpoch, periodoSegundos_) : vector<Punto>{};
    const auto puntosRc = rRc ? puntosCompletos(*rRc, ahoraEpoch, periodoSegundos_) : vector<Punto>{};
    const auto puntosHh = rHh ? puntosCompletos(*rHh, ahoraEpoch, periodoSegundos_) : vector<Punto>{};

    // El periodo del ciclo es el mas reciente para el que CUALQUIERA de las 3 metricas
    // tenga un dato completo: si a alguna le falta justo ese periodo, la deja en null
    // (regla 4.6), pero no se pierde el ciclo entero solo porque una metrica llegue tarde.
    DataTs dataTs = -1;
    if (!puntosCpu.empty()) dataTs = max(dataTs, puntosCpu.back().inicio);
    if (!puntosRc.empty())  dataTs = max(dataTs, puntosRc.back().inicio);
    if (!puntosHh.empty())  dataTs = max(dataTs, puntosHh.back().inicio);
    if (dataTs < 0) {
        // Ninguna de las 3 tiene ni un solo dato completo en toda la ventana de busqueda.
        return nullopt;
    }

    MetricSnapshot s;
    s.dataTs = dataTs;
    s.periodoSegundos = periodoSegundos_;
    s.cpu = valorEnPeriodo(puntosCpu, dataTs);
    s.requestCount = valorRequestCount(puntosRc, dataTs, periodoSegundos_);
    s.healthyHosts = valorEnPeriodo(puntosHh, dataTs);

    Aws::AutoScaling::Model::DescribeAutoScalingGroupsRequest asReq;
    asReq.AddAutoScalingGroupNames(asgName_);
    const auto asOutcome = asClient_.DescribeAutoScalingGroups(asReq);
    if (asOutcome.IsSuccess() && !asOutcome.GetResult().GetAutoScalingGroups().empty()) {
        const auto& grupo = asOutcome.GetResult().GetAutoScalingGroups()[0];
        s.desired = grupo.GetDesiredCapacity();
        int inService = 0, pending = 0;
        for (const auto& inst : grupo.GetInstances()) {
            switch (inst.GetLifecycleState()) {
                case Aws::AutoScaling::Model::LifecycleState::InService: inService++; break;
                case Aws::AutoScaling::Model::LifecycleState::Pending:
                case Aws::AutoScaling::Model::LifecycleState::Pending_Wait:
                case Aws::AutoScaling::Model::LifecycleState::Pending_Proceed: pending++; break;
                default: break;   // Terminating/Standby/etc: ni sirven ni estan llegando
            }
        }
        s.inService = inService;
        s.pending = pending;
    }
    // Si DescribeAutoScalingGroups falla, desired/inService/pending quedan en nullopt:
    // evaluarCalidad() ya lo interpreta como dato incompleto sin que haga falta duplicar
    // aqui el motivo.

    return s;
}
