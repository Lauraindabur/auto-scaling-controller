#pragma once
#include <optional>
#include <string>
#include <aws/autoscaling/AutoScalingClient.h>
#include <aws/monitoring/CloudWatchClient.h>
#include "MetricSnapshot.hpp"

// Las 3 metricas de decision (seccion 4.2) en una sola llamada GetMetricData, mas
// DescribeAutoScalingGroups para desired/InService/Pending.
class CloudWatchMetricSource {
public:
    // loadBalancerDim / targetGroupDim son las dimensiones tal como las publica el ALB
    // (app/<nombre>/<id> y targetgroup/<nombre>/<id>), ya listas para usar: se configuran
    // directo (LOAD_BALANCER_DIM/TARGET_GROUP_DIM), no se derivan de un ARN.
    CloudWatchMetricSource(std::string asgName, std::string loadBalancerDim,
                           std::string targetGroupDim, const std::string& region,
                           int periodoSegundos);

    // Ultimo periodo COMPLETO de CloudWatch (se descarta el minuto en curso). nullopt =
    // no se pudo determinar ni el timestamp del periodo (fallo total de la fuente).
    std::optional<MetricSnapshot> ultimoPeriodoCompleto();

private:
    Aws::CloudWatch::Model::GetMetricDataRequest construirRequest(
        const Aws::Utils::DateTime& ahora) const;

    std::string asgName_;
    std::string loadBalancerDim_;
    std::string targetGroupDim_;
    int periodoSegundos_;
    Aws::CloudWatch::CloudWatchClient cwClient_;
    Aws::AutoScaling::AutoScalingClient asClient_;
};
