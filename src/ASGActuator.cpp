#include "ASGActuator.hpp"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/autoscaling/model/SetDesiredCapacityRequest.h>

ASGActuator::ASGActuator(std::string asgName, const std::string& region)
    : asgName_(std::move(asgName)) {
    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.retryStrategy = Aws::MakeShared<Aws::Client::DefaultRetryStrategy>("ASGActuator", 3);
    asClient_ = Aws::AutoScaling::AutoScalingClient(config);
}

ResultadoAccion ASGActuator::fijarCapacidad(int desired) {
    Aws::AutoScaling::Model::SetDesiredCapacityRequest req;
    req.SetAutoScalingGroupName(asgName_);
    req.SetDesiredCapacity(desired);
    req.SetHonorCooldown(false);   // el cooldown lo maneja el controller, no el ASG

    const auto outcome = asClient_.SetDesiredCapacity(req);
    if (outcome.IsSuccess()) return {true, "OK"};
    return {false, outcome.GetError().GetMessage()};
}
