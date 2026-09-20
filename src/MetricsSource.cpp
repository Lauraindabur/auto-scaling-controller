#include "MetricSource.hpp"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/monitoring/model/Dimension.h>
#include <aws/core/utils/DateTime.h>

std::string MetricSource::extraerDimensionValue(const std::string& arn, const std::string& prefijo) {
    auto pos = arn.find(prefijo);
    if (pos == std::string::npos) {
        return "";
    }
    return arn.substr(pos);
}

MetricSource::MetricSource(std::string asgName, std::string loadBalancerArn,
                            std::string targetGroupArn, const std::string& region)
    : asgName_(std::move(asgName)) {
    lbDimensionValue_ = extraerDimensionValue(loadBalancerArn, "app/");
    tgDimensionValue_ = extraerDimensionValue(targetGroupArn, "targetgroup/");

    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.retryStrategy = Aws::MakeShared<Aws::Client::DefaultRetryStrategy>("MetricSource", 3);
    client_ = Aws::CloudWatch::CloudWatchClient(config);
}

Aws::CloudWatch::Model::GetMetricStatisticsRequest MetricSource::construirRequestCPU(
    std::chrono::minutes atras) {
    Aws::CloudWatch::Model::GetMetricStatisticsRequest req;
    req.SetNamespace("AWS/EC2");
    req.SetMetricName("CPUUtilization");

    Aws::CloudWatch::Model::Dimension dim;
    dim.SetName("AutoScalingGroupName");
    dim.SetValue(asgName_);
    req.AddDimensions(dim);

    req.SetStartTime(Aws::Utils::DateTime::Now() - atras);
    req.SetEndTime(Aws::Utils::DateTime::Now());
    req.SetPeriod(60);
    req.AddStatistics(Aws::CloudWatch::Model::Statistic::Average);
    return req;
}

MetricSource::Lectura MetricSource::obtenerActual() {
    auto request = construirRequestCPU(std::chrono::minutes(2));
    auto outcome = client_.GetMetricStatistics(request);

    if (!outcome.IsSuccess()) return {false, 0.0};
    auto puntos = outcome.GetResult().GetDatapoints();
    if (puntos.empty()) return {false, 0.0};
    return {true, puntos.back().GetAverage()};
}

std::vector<double> MetricSource::obtenerHistorialInicial(std::size_t maxPuntos) {
    auto request = construirRequestCPU(std::chrono::minutes(static_cast<long>(maxPuntos)));
    auto outcome = client_.GetMetricStatistics(request);

    std::vector<double> resultado;
    if (!outcome.IsSuccess()) return resultado;
    for (auto& p : outcome.GetResult().GetDatapoints()) {
        resultado.push_back(p.GetAverage());
    }
    return resultado;
}

MetricSource::MetricasSecundarias MetricSource::obtenerMetricasSecundarias() {
    if (lbDimensionValue_.empty() || tgDimensionValue_.empty()) {
        return {false, 0.0, 0.0};
    }

    Aws::CloudWatch::Model::Dimension dimLB;
    dimLB.SetName("LoadBalancer");
    dimLB.SetValue(lbDimensionValue_);

    Aws::CloudWatch::Model::Dimension dimTG;
    dimTG.SetName("TargetGroup");
    dimTG.SetValue(tgDimensionValue_);

    Aws::CloudWatch::Model::GetMetricStatisticsRequest reqTRT;
    reqTRT.SetNamespace("AWS/ApplicationELB");
    reqTRT.SetMetricName("TargetResponseTime");
    reqTRT.AddDimensions(dimLB);
    reqTRT.AddDimensions(dimTG);
    reqTRT.SetStartTime(Aws::Utils::DateTime::Now() - std::chrono::minutes(2));
    reqTRT.SetEndTime(Aws::Utils::DateTime::Now());
    reqTRT.SetPeriod(60);
    reqTRT.AddStatistics(Aws::CloudWatch::Model::Statistic::Average);

    auto outcomeTRT = client_.GetMetricStatistics(reqTRT);
    if (!outcomeTRT.IsSuccess() || outcomeTRT.GetResult().GetDatapoints().empty()) {
        return {false, 0.0, 0.0};
    }
    double trt = outcomeTRT.GetResult().GetDatapoints().back().GetAverage();

    Aws::CloudWatch::Model::GetMetricStatisticsRequest reqRCPT;
    reqRCPT.SetNamespace("AWS/ApplicationELB");
    reqRCPT.SetMetricName("RequestCountPerTarget");
    reqRCPT.AddDimensions(dimLB);
    reqRCPT.AddDimensions(dimTG);
    reqRCPT.SetStartTime(Aws::Utils::DateTime::Now() - std::chrono::minutes(2));
    reqRCPT.SetEndTime(Aws::Utils::DateTime::Now());
    reqRCPT.SetPeriod(60);
    reqRCPT.AddStatistics(Aws::CloudWatch::Model::Statistic::Sum);

    auto outcomeRCPT = client_.GetMetricStatistics(reqRCPT);
    if (!outcomeRCPT.IsSuccess() || outcomeRCPT.GetResult().GetDatapoints().empty()) {
        return {false, trt, 0.0};
    }
    double rcpt = outcomeRCPT.GetResult().GetDatapoints().back().GetSum();

    return {true, trt, rcpt};
}