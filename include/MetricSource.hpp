#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <aws/monitoring/CloudWatchClient.h>  // Clases del AWS  SDK para el cliente de Cloudwatch y la peeticion Getmetricstatistics 
#include <aws/monitoring/model/GetMetricStatisticsRequest.h>
#include "IMetricSource.hpp" // -> interfaz que esta clase cumple/implementa

using namespace std;

class MetricSource : public IMetricSource {   // Implementacion de la interfaz IMetricSource 
public:
    // Lanza runtime_error si los ARN no permiten derivar las dimensiones del ALB / Target Group.
    MetricSource(string asgName, string loadBalancerArn, string targetGroupArn, const string& region);

    Lectura obtenerActual() override;
    vector<double> obtenerHistorialInicial(size_t maxPuntos);
    vector<double> obtenerHistorialInicialRT(size_t maxPuntos);
    MetricasSecundarias obtenerMetricasSecundarias() override;

private:
    Aws::CloudWatch::Model::GetMetricStatisticsRequest construirRequestCPU(
        chrono::minutes atras);
    Aws::CloudWatch::Model::GetMetricStatisticsRequest construirRequestELB(
        const string& metrica, Aws::CloudWatch::Model::Statistic estadistico,
        chrono::minutes atras);
    static string extraerDimensionValue(const string& arn, const string& prefijo);

    string asgName_;
    string lbDimensionValue_;
    string tgDimensionValue_;
    Aws::CloudWatch::CloudWatchClient client_;
};
