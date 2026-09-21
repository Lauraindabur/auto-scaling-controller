#include "MetricSource.hpp"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/monitoring/model/Dimension.h>
#include <aws/core/utils/DateTime.h>
#include <aws/monitoring/model/Datapoint.h>
#include <algorithm>
#include <stdexcept>

using namespace std;

namespace {

// CloudWatch no garantiza orden cronologico en los datapoints devueltos.
Aws::Vector<Aws::CloudWatch::Model::Datapoint> ordenarPorTiempo(
    Aws::Vector<Aws::CloudWatch::Model::Datapoint> puntos) {
    sort(puntos.begin(), puntos.end(),
         [](const Aws::CloudWatch::Model::Datapoint& a,
            const Aws::CloudWatch::Model::Datapoint& b) {
             return a.GetTimestamp() < b.GetTimestamp();
         });
    return puntos;
}

// Resultado de consultar el ultimo datapoint de una metrica:
// distingue "la llamada fallo" (apiOk = false) de "respondio sin datapoints" (hayDatos = false).
struct Consulta {
    bool apiOk;
    bool hayDatos;
    double valor;
};

Consulta ultimoValor(Aws::CloudWatch::CloudWatchClient& client,
                     const Aws::CloudWatch::Model::GetMetricStatisticsRequest& request,
                     bool usarSuma) {
    auto outcome = client.GetMetricStatistics(request);
    if (!outcome.IsSuccess()) return {false, false, 0.0};
    auto puntos = ordenarPorTiempo(outcome.GetResult().GetDatapoints());
    if (puntos.empty()) return {true, false, 0.0};
    return {true, true, usarSuma ? puntos.back().GetSum() : puntos.back().GetAverage()};
}

} // namespace

string MetricSource::extraerDimensionValue(const string& arn, const string& prefijo) {
    auto pos = arn.find(prefijo);
    if (pos == string::npos) {
        return "";
    }
    return arn.substr(pos);
}

MetricSource::MetricSource(string asgName, string loadBalancerArn,
                            string targetGroupArn, const string& region)
    : asgName_(move(asgName)) {
    lbDimensionValue_ = extraerDimensionValue(loadBalancerArn, "app/");
    tgDimensionValue_ = extraerDimensionValue(targetGroupArn, "targetgroup/");

    // TargetResponseTime es metrica de decision: sin estas dimensiones el controller
    // fallaria todos los ciclos, asi que se aborta al iniciar.
    if (lbDimensionValue_.empty()) {
        throw runtime_error(
            "LOAD_BALANCER_ARN invalido: se esperaba un ARN de Application Load Balancer "
            "que contenga 'app/' (arn:aws:elasticloadbalancing:<region>:<cuenta>:loadbalancer/app/<nombre>/<id>), "
            "valor recibido: '" + loadBalancerArn + "'");
    }
    if (tgDimensionValue_.empty()) {
        throw runtime_error(
            "TARGET_GROUP_ARN invalido: se esperaba un ARN de Target Group que contenga "
            "'targetgroup/' (arn:aws:elasticloadbalancing:<region>:<cuenta>:targetgroup/<nombre>/<id>), "
            "valor recibido: '" + targetGroupArn + "'");
    }

    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.retryStrategy = Aws::MakeShared<Aws::Client::DefaultRetryStrategy>("MetricSource", 3);
    client_ = Aws::CloudWatch::CloudWatchClient(config);
}

Aws::CloudWatch::Model::GetMetricStatisticsRequest MetricSource::construirRequestCPU(
    chrono::minutes atras) {
    Aws::CloudWatch::Model::GetMetricStatisticsRequest req;
    req.SetNamespace("AWS/EC2");
    req.SetMetricName("CPUUtilization");

    Aws::CloudWatch::Model::Dimension dim;
    dim.SetName("AutoScalingGroupName");
    dim.SetValue(asgName_);
    req.AddDimensions(dim);

    auto ahora = Aws::Utils::DateTime::Now();
    req.SetStartTime(ahora - atras);
    req.SetEndTime(ahora);
    req.SetPeriod(60);
    req.AddStatistics(Aws::CloudWatch::Model::Statistic::Average);
    return req;
}

Aws::CloudWatch::Model::GetMetricStatisticsRequest MetricSource::construirRequestELB(
    const string& metrica, Aws::CloudWatch::Model::Statistic estadistico,
    chrono::minutes atras) {
    Aws::CloudWatch::Model::Dimension dimLB;
    dimLB.SetName("LoadBalancer");
    dimLB.SetValue(lbDimensionValue_);

    Aws::CloudWatch::Model::Dimension dimTG;
    dimTG.SetName("TargetGroup");
    dimTG.SetValue(tgDimensionValue_);

    Aws::CloudWatch::Model::GetMetricStatisticsRequest req;
    req.SetNamespace("AWS/ApplicationELB");
    req.SetMetricName(metrica);
    req.AddDimensions(dimLB);
    req.AddDimensions(dimTG);

    auto ahora = Aws::Utils::DateTime::Now();
    req.SetStartTime(ahora - atras);
    req.SetEndTime(ahora);
    req.SetPeriod(60);
    req.AddStatistics(estadistico);
    return req;
}

MetricSource::Lectura MetricSource::obtenerActual() {
    // Se consultan siempre las dos para poder informar cual fallo.
    auto cpu = ultimoValor(client_, construirRequestCPU(chrono::minutes(2)), false);
    auto rt = ultimoValor(client_,
        construirRequestELB("TargetResponseTime",
                            Aws::CloudWatch::Model::Statistic::Average, chrono::minutes(2)),
        false);

    bool cpuOk = cpu.apiOk && cpu.hayDatos;
    // RT: error de API = fallo real; respuesta sin datapoints = sin requests = 0.0 s.
    bool rtOk = rt.apiOk;
    double rtValor = rt.hayDatos ? rt.valor : 0.0;

    return {cpuOk && rtOk, cpuOk, rtOk, cpuOk ? cpu.valor : 0.0, rtOk ? rtValor : 0.0};
}

vector<double> MetricSource::obtenerHistorialInicial(size_t maxPuntos) {
    auto request = construirRequestCPU(chrono::minutes(static_cast<long>(maxPuntos)));
    auto outcome = client_.GetMetricStatistics(request);

    vector<double> resultado;
    if (!outcome.IsSuccess()) return resultado;
    for (auto& p : ordenarPorTiempo(outcome.GetResult().GetDatapoints())) {
        resultado.push_back(p.GetAverage());
    }
    return resultado;
}

vector<double> MetricSource::obtenerHistorialInicialRT(size_t maxPuntos) {
    auto request = construirRequestELB("TargetResponseTime",
                                       Aws::CloudWatch::Model::Statistic::Average,
                                       chrono::minutes(static_cast<long>(maxPuntos)));
    auto outcome = client_.GetMetricStatistics(request);

    // Sin datapoints no se inventa historial: los ciclos nuevos lo iran completando.
    vector<double> resultado;
    if (!outcome.IsSuccess()) return resultado;
    for (auto& p : ordenarPorTiempo(outcome.GetResult().GetDatapoints())) {
        resultado.push_back(p.GetAverage());
    }
    return resultado;
}

MetricSource::MetricasSecundarias MetricSource::obtenerMetricasSecundarias() {
    auto rcpt = ultimoValor(client_,
        construirRequestELB("RequestCountPerTarget",
                            Aws::CloudWatch::Model::Statistic::Sum, chrono::minutes(2)),
        true);
    if (!rcpt.apiOk || !rcpt.hayDatos) return {false, 0.0};
    return {true, rcpt.valor};
}
