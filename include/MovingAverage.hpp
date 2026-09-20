#pragma once
#include <deque>
#include <vector>
#include <numeric>

using namespace std;

class MovingAverage {
public:
    explicit MovingAverage(size_t ventana) : ventana_(ventana) {}

    void agregar(double valor) {
        buffer_.push_back(valor);
        if (buffer_.size() > ventana_) {
            buffer_.pop_front();
        }
    }

    void prellenar(const vector<double>& historicos) {
        for (double v : historicos) {
            agregar(v);
        }
    }

    bool tieneSuficienteHistorial() const {
        return buffer_.size() == ventana_;
    }

    size_t muestrasActuales() const {
        return buffer_.size();
    }

    double valor() const {
        double suma = accumulate(buffer_.begin(), buffer_.end(), 0.0);
        return suma / static_cast<double>(buffer_.size());
    }

private:
    deque<double> buffer_;
    size_t ventana_;
};