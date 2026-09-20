#pragma once
#include <string>
#include <aws/autoscaling/AutoScalingClient.h>
#include <aws/elasticloadbalancingv2/ElasticLoadBalancingv2Client.h>

class ASGActuator {
public:
    ASGActuator(std::string asgName, const std::string& region,
                std::string targetGroupArn, int capacidadMin, int capacidadMax);

    int capacidadActual();
    bool ejecutar(const std::string& decision);
    bool instanciasRestantesSanas();
    bool operacionTermino();

private:
    std::string asgName_;
    std::string targetGroupArn_;
    int capacidadMin_;
    int capacidadMax_;
    Aws::AutoScaling::AutoScalingClient asClient_;
    Aws::ElasticLoadBalancingv2::ElasticLoadBalancingv2Client elbClient_;
};