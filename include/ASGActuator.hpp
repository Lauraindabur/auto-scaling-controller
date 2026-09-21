#pragma once
#include <string>
#include "IActuator.hpp"
#include <aws/autoscaling/AutoScalingClient.h>
#include <aws/elasticloadbalancingv2/ElasticLoadBalancingv2Client.h>

class ASGActuator : public IActuator {
public:
    ASGActuator(std::string asgName, const std::string& region,
                std::string targetGroupArn, int capacidadMin, int capacidadMax);

    int capacidadActual() override;
    bool ejecutar(const std::string& decision) override;
    bool instanciasRestantesSanas() override;
    bool operacionTermino() override;

private:
    std::string asgName_;
    std::string targetGroupArn_;
    int capacidadMin_;
    int capacidadMax_;
    Aws::AutoScaling::AutoScalingClient asClient_;
    Aws::ElasticLoadBalancingv2::ElasticLoadBalancingv2Client elbClient_;
};