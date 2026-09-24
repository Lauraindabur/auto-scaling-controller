#pragma once
#include <deque>  // una fila donde se mete or un lado y se saca por el otro
#include <vector>
#include <numeric>

using namespace std;

class MovingAverage {
public:     //todo esto es usado desde main si es necesario
    explicit MovingAverage(size_t ventana) : ventana_(ventana) {}  //constructor que recibe el tamaño de la ventana,copia ese valor en ventana_ se usa explicit apra evitar una conversion al tener el cosntructor un solo

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

    // Contenido de la ventana, de la muestra mas vieja a la mas nueva. Es lo que el
    // StateStore persiste para que un reinicio no tenga que volver a llenar la ventana.
    vector<double> valores() const {
        return vector<double>(buffer_.begin(), buffer_.end());
    }

    double valor() const {
        double suma = accumulate(buffer_.begin(), buffer_.end(), 0.0);
        return suma / static_cast<double>(buffer_.size());
    }

private:
    deque<double> buffer_;
    size_t ventana_;
};