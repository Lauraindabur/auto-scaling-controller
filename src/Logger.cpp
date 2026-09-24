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

namespace {

std::string numONull(const std::optional<double>& v) {
    return v ? std::to_string(*v) : "null";
}

std::string enteroONull(const std::optional<int>& v) {
    return v ? std::to_string(*v) : "null";
}

} // namespace

void Logger::registrar(const RegistroCiclo& r) {
    cycleId_++;
    std::ostringstream json;
    json << "{"
         << "\"timestamp\":\"" << timestampActual() << "\","
         << "\"cycle_id\":" << cycleId_ << ","
         << "\"data_ts\":" << r.dataTs << ","
         << "\"period_s\":" << r.periodoSegundos << ","
         << "\"ma_window\":" << r.ventanaMa << ","

         << "\"cpu\":" << numONull(r.cpu) << ","
         << "\"request_count\":" << numONull(r.requestCount) << ","
         << "\"healthy_hosts\":" << numONull(r.healthyHosts) << ","

         << "\"data_quality\":\"" << nombreCalidad(r.calidad) << "\","
         << "\"data_issues\":[";
    for (size_t i = 0; i < r.dataIssues.size(); i++) {
        if (i) json << ",";
        json << "\"" << escapar(r.dataIssues[i]) << "\"";
    }
    json << "],"

         << "\"ma_cpu\":" << numONull(r.maCpu) << ","
         << "\"reactive_signal\":\"" << nombreSenal(r.reactiveSignal) << "\","

         << "\"holt_level\":" << numONull(r.holtLevel) << ","
         << "\"holt_trend\":" << numONull(r.holtTrend) << ","
         << "\"forecast_rpm\":" << numONull(r.forecastRpm) << ","
         << "\"horizon\":" << r.horizon << ","
         << "\"needed_instances\":" << enteroONull(r.neededInstances) << ","
         << "\"proactive_signal\":\"" << nombreSenal(r.proactiveSignal) << "\","
         << "\"spike_guard_applied\":" << (r.spikeGuardApplied ? "true" : "false") << ","

         << "\"desired\":" << enteroONull(r.desired) << ","
         << "\"in_service\":" << enteroONull(r.inService) << ","
         << "\"pending\":" << enteroONull(r.pending) << ","

         << "\"guards\":[";
    for (size_t i = 0; i < r.guards.size(); i++) {
        if (i) json << ",";
        const auto& g = r.guards[i];
        json << "{\"name\":\"" << escapar(g.nombre) << "\","
             << "\"passed\":" << (g.paso ? "true" : "false") << ","
             << "\"detail\":\"" << escapar(g.detalle) << "\","
             << "\"warning\":" << (g.advertencia ? "true" : "false") << "}";
    }
    json << "],"
         << "\"blocked_by\":" << (r.blockedBy.empty() ? "null" : "\"" + escapar(r.blockedBy) + "\"") << ","

         << "\"decision\":\"" << nombreDecision(r.decision) << "\","
         << "\"trigger\":\"" << nombreTrigger(r.trigger) << "\","
         << "\"justification\":\"" << escapar(r.justification) << "\","

         << "\"action_requested\":\"" << escapar(r.actionRequested) << "\","
         << "\"action_result\":\"" << escapar(r.actionResult) << "\""
         << "}";
    escribirLinea(json.str());
}
