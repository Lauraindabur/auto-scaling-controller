#pragma once

using namespace std;

enum class OperationState { NONE, IN_PROGRESS, SUCCESSFUL, FAILED };

class CooldownManager {
public:
    explicit CooldownManager(int ciclosCooldown) : ciclosCooldown_(ciclosCooldown) {}

    bool cooldownActivo() const {
        return ciclosRestantes_ > 0;
    }

    void decrementarCooldown() {
        if (ciclosRestantes_ > 0) {
            ciclosRestantes_--;
        }
    }

    int ciclosRestantes() const {
        return ciclosRestantes_;
    }

    OperationState estado() const {
        return estado_;
    }

    void marcarEnProgreso() {
        estado_ = OperationState::IN_PROGRESS;
    }

    void marcarExitosa() {
        estado_ = OperationState::NONE;
        ciclosRestantes_ = ciclosCooldown_;
    }

    void marcarFallida() {
        estado_ = OperationState::NONE;
    }

    // Timeout: la operacion se da por fallida, pero el estado real es incierto,
    // asi que (a diferencia de marcarFallida) si arranca el cooldown.
    void marcarTimeout() {
        estado_ = OperationState::NONE;
        ciclosRestantes_ = ciclosCooldown_;
    }

private:
    int ciclosCooldown_;
    int ciclosRestantes_ = 0;
    OperationState estado_ = OperationState::NONE;
};