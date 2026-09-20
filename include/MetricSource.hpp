#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <aws/monitoring/CloudWatchClient.h>
#include <aws/monitoring/model/GetMetricStatisticsRequest.h>

class MetricSource {
public:
    MetricSource(std::string asgName, std::string loadBalancerArn,
                 std::string targetGroupArn, const std::string& region);

    struct Lectura {
        bool exito;
        double valor;
    };

    struct MetricasSecundarias {
        bool exito;
        double targetResponseTime;
        double requestCountPerTarget;
    };

    Lectura obtenerActual();
    std::vector<double> obtenerHistorialInicial(std::size_t maxPuntos);
    MetricasSecundarias obtenerMetricasSecundarias();

private:
    Aws::CloudWatch::Model::GetMetricStatisticsRequest construirRequestCPU(
        std::chrono::minutes atras);
    static std::string extraerDimensionValue(const std::string& arn, const std::string& prefijo);

    std::string asgName_;
    std::string lbDimensionValue_;
    std::string tgDimensionValue_;
    Aws::CloudWatch::CloudWatchClient client_;
};