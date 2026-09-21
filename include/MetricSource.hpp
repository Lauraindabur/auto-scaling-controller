#pragma once
#include <string>
#include <vector>
#include <chrono>
#include "IMetricSource.hpp"
#include <aws/monitoring/CloudWatchClient.h>
#include <aws/monitoring/model/GetMetricStatisticsRequest.h>

using namespace std;

class MetricSource : public IMetricSource {
public:
    MetricSource(string asgName, string loadBalancerArn,
                 string targetGroupArn, const string& region);

    Lectura obtenerActual() override;
    vector<double> obtenerHistorialInicial(size_t maxPuntos);
    MetricasSecundarias obtenerMetricasSecundarias() override;

private:
    Aws::CloudWatch::Model::GetMetricStatisticsRequest construirRequestCPU(
        chrono::minutes atras);
    static string extraerDimensionValue(const string& arn, const string& prefijo);

    string asgName_;
    string lbDimensionValue_;
    string tgDimensionValue_;
    Aws::CloudWatch::CloudWatchClient client_;
};