#include "Logger.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdexcept>

Logger::Logger(const std::string& rutaArchivo) {
    archivo_.open(rutaArchivo, std::ios::app);
    if (!archivo_.is_open()) {
        throw std::runtime_error("No se pudo abrir el archivo de log: " + rutaArchivo);
    }
}

Logger::~Logger() {
    if (archivo_.is_open()) {
        archivo_.close();
    }
}

std::string Logger::timestampActual() const {
    auto ahora = std::chrono::system_clock::now();
    std::time_t tiempo = std::chrono::system_clock::to_time_t(ahora);
    std::tm tm_utc{};
    gmtime_r(&tiempo, &tm_utc);

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string Logger::escapar(const std::string& texto) const {
    std::string resultado;
    resultado.reserve(texto.size());
    for (char c : texto) {
        if (c == '"' || c == '\\') resultado += '\\';
        resultado += c;
    }
    return resultado;
}

void Logger::escribirLinea(const std::string& json) {
    archivo_ << json << "\n";
    archivo_.flush();
}

static std::string numeroONull(const std::optional<double>& v) {
    return v ? std::to_string(*v) : "null";
}

void Logger::registrar(const RegistroCiclo& r) {
    cycleId_++;
    std::ostringstream json;
    json << "{"
         << "\"timestamp\":\"" << timestampActual() << "\","
         << "\"cycle_id\":" << cycleId_ << ","
         << "\"cpu_utilization\":" << numeroONull(r.cpuUtilization) << ","
         << "\"moving_average_cpu\":" << numeroONull(r.movingAverageCpu) << ","
         << "\"target_response_time\":" << numeroONull(r.targetResponseTime) << ","
         << "\"moving_average_response_time\":" << numeroONull(r.movingAverageResponseTime) << ","
         << "\"current_capacity\":"
         << (r.capacidadActual ? std::to_string(*r.capacidadActual) : "null") << ","
         << "\"decision\":\"" << escapar(r.decision) << "\","
         << "\"decision_trigger\":\"" << escapar(r.decisionTrigger) << "\","
         << "\"justification\":\"" << escapar(r.justificacion) << "\","
         << "\"requested_action\":\"" << escapar(r.accionSolicitada) << "\","
         << "\"action_result\":\"" << escapar(r.resultadoAccion) << "\","
         << "\"operation_state\":\"" << escapar(r.estadoOperacion) << "\","
         << "\"cooldown_remaining\":" << r.cooldownRestante << ","
         << "\"request_count_per_target\":" << numeroONull(r.requestCountPerTarget)
         << "}";
    escribirLinea(json.str());
}
