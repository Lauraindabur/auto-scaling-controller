#include "ASGActuator.hpp"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/autoscaling/model/DescribeAutoScalingGroupsRequest.h>
#include <aws/autoscaling/model/SetDesiredCapacityRequest.h>
#include <aws/elasticloadbalancingv2/model/DescribeTargetHealthRequest.h>

ASGActuator::ASGActuator(std::string asgName, const std::string& region,
                          std::string targetGroupArn, int capacidadMin, int capacidadMax)
    : asgName_(std::move(asgName)), targetGroupArn_(std::move(targetGroupArn)),
      capacidadMin_(capacidadMin), capacidadMax_(capacidadMax) {
    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.retryStrategy = Aws::MakeShared<Aws::Client::DefaultRetryStrategy>("ASGActuator", 3);
    asClient_ = Aws::AutoScaling::AutoScalingClient(config);
    elbClient_ = Aws::ElasticLoadBalancingv2::ElasticLoadBalancingv2Client(config);
}

int ASGActuator::capacidadActual() {
    Aws::AutoScaling::Model::DescribeAutoScalingGroupsRequest req;
    req.AddAutoScalingGroupNames(asgName_);
    auto outcome = asClient_.DescribeAutoScalingGroups(req);

    if (!outcome.IsSuccess() || outcome.GetResult().GetAutoScalingGroups().empty()) {
        return -1;
    }
    return outcome.GetResult().GetAutoScalingGroups()[0].GetDesiredCapacity();
}

bool ASGActuator::ejecutar(const std::string& decision, int paso) {
    int actual = capacidadActual();
    if (actual < 0) return false;

    int nueva = actual;
    if (decision == "INCREASE_CAPACITY") nueva = actual + paso;
    else if (decision == "REDUCE_CAPACITY") nueva = actual - 1;

    if (nueva > capacidadMax_) nueva = capacidadMax_;
    if (nueva < capacidadMin_) nueva = capacidadMin_;

    Aws::AutoScaling::Model::SetDesiredCapacityRequest req;
    req.SetAutoScalingGroupName(asgName_);
    req.SetDesiredCapacity(nueva);

    auto outcome = asClient_.SetDesiredCapacity(req);
    return outcome.IsSuccess();
}

bool ASGActuator::instanciasRestantesSanas() {
    Aws::ElasticLoadBalancingv2::Model::DescribeTargetHealthRequest req;
    req.SetTargetGroupArn(targetGroupArn_);
    auto outcome = elbClient_.DescribeTargetHealth(req);

    if (!outcome.IsSuccess()) return false;
    if (outcome.GetResult().GetTargetHealthDescriptions().empty()) return false;

    for (auto& desc : outcome.GetResult().GetTargetHealthDescriptions()) {
        if (desc.GetTargetHealth().GetState() !=
            Aws::ElasticLoadBalancingv2::Model::TargetHealthStateEnum::healthy) {
            return false;
        }
    }
    return true;
}

bool ASGActuator::operacionTermino() {
    int deseada = capacidadActual();
    if (deseada < 0) return false;

    Aws::ElasticLoadBalancingv2::Model::DescribeTargetHealthRequest req;
    req.SetTargetGroupArn(targetGroupArn_);
    auto outcome = elbClient_.DescribeTargetHealth(req);
    if (!outcome.IsSuccess()) return false;

    // Terminada = ni mas ni menos targets que la capacidad deseada (sin instancias
    // en draining/initial/terminando) y todos Healthy.
    int total = 0;
    int sanas = 0;
    for (auto& desc : outcome.GetResult().GetTargetHealthDescriptions()) {
        total++;
        if (desc.GetTargetHealth().GetState() ==
            Aws::ElasticLoadBalancingv2::Model::TargetHealthStateEnum::healthy) {
            sanas++;
        }
    }
    return total == deseada && sanas == deseada;
}