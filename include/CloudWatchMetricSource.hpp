#pragma once
#include <optional>
#include <string>
#include <aws/autoscaling/AutoScalingClient.h>
#include <aws/monitoring/CloudWatchClient.h>
#include "MetricSnapshot.hpp"

using namespace std;


// Se llama a  CloudWatch para CPU promedio de las instancias, peticiones que recibio el ALB e instancias sanas.
// al Auto Scaling Group: cuantas instancias hay pedidas, cuantas corriendo y cuantas arrancando.
// Todo eso lo junta en un MetricSnapshot, que es lo que usa el controller para decidir.
class CloudWatchMetricSource {
public:
    // loadBalancerDim y targetGroupDim -> nombres cortos del ALB y del Target Group 
    CloudWatchMetricSource(string asgName, string loadBalancerDim,
                           string targetGroupDim, const string& region,
                           int periodoSegundos);

    optional<MetricSnapshot> ultimoPeriodoCompleto();

private:
    // Acá arma la consulta a CloudWatch con las 3 metricas.
    Aws::CloudWatch::Model::GetMetricDataRequest construirRequest(
        const Aws::Utils::DateTime& ahora) const;

    string asgName_;
    string loadBalancerDim_;
    string targetGroupDim_;
    int periodoSegundos_;
    Aws::CloudWatch::CloudWatchClient cwClient_;
    Aws::AutoScaling::AutoScalingClient asClient_;
};
