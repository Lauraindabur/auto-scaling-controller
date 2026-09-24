#include "StateStore.hpp"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace {

// --- Escritura: helpers minimos, en el mismo estilo que Logger.cpp (sin libreria JSON). ---

// Formato corto (no to_string, que fuerza 6 decimales): mas legible al inspeccionar el
// archivo a mano, y el parser de abajo entiende cualquiera de las dos formas igual.
string numOJson(optional<double> v) {
    if (!v) return "null";
    ostringstream oss; oss << *v; return oss.str();
}
string tsOJson(optional<DataTs> v) { return v ? to_string(*v) : "null"; }

string arregloDoubles(const vector<double>& v) {
    ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < v.size(); i++) {
        if (i) oss << ", ";
        oss << v[i];
    }
    oss << "]";
    return oss.str();
}

// --- Lectura: parser JSON minimo, suficiente para el esquema fijo que este archivo
// escribe (objetos, arreglos, numeros, bool, null; las claves son siempre texto simple
// sin caracteres especiales). Cualquier desviacion lanza runtime_error, que cargar()
// atrapa y convierte en "estado vacio" en vez de romper el arranque. ---

struct JsonValor {
    enum Tipo { NULO, BOOLEANO, NUMERO, ARREGLO, OBJETO } tipo = NULO;
    bool b = false;
    double n = 0.0;
    vector<JsonValor> arreglo;
    map<string, JsonValor> objeto;

    optional<double> comoDouble() const {
        return tipo == NUMERO ? optional<double>(n) : nullopt;
    }
    optional<DataTs> comoTs() const {
        return tipo == NUMERO ? optional<DataTs>(static_cast<DataTs>(n)) : nullopt;
    }
    bool comoBool(bool porDefecto) const { return tipo == BOOLEANO ? b : porDefecto; }
    vector<double> comoVectorDoubles() const {
        vector<double> r;
        if (tipo == ARREGLO) for (const auto& e : arreglo) if (e.tipo == NUMERO) r.push_back(e.n);
        return r;
    }
    const JsonValor* buscar(const string& clave) const {
        if (tipo != OBJETO) return nullptr;
        auto it = objeto.find(clave);
        return it == objeto.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const string& texto) : s_(texto) {}

    JsonValor parsear() {
        JsonValor v = parsearValor();
        saltarEspacios();
        if (pos_ != s_.size()) throw runtime_error("texto sobrante tras el JSON");
        return v;
    }

private:
    void saltarEspacios() { while (pos_ < s_.size() && isspace(static_cast<unsigned char>(s_[pos_]))) pos_++; }
    char actual() const { if (pos_ >= s_.size()) throw runtime_error("JSON incompleto"); return s_[pos_]; }
    void esperar(char c) {
        saltarEspacios();
        if (actual() != c) throw runtime_error(string("se esperaba '") + c + "'");
        pos_++;
    }

    JsonValor parsearValor() {
        saltarEspacios();
        char c = actual();
        if (c == '{') return parsearObjeto();
        if (c == '[') return parsearArreglo();
        if (c == 't' || c == 'f') return parsearBool();
        if (c == 'n') return parsearNull();
        return parsearNumero();
    }

    JsonValor parsearObjeto() {
        JsonValor v; v.tipo = JsonValor::OBJETO;
        esperar('{');
        saltarEspacios();
        if (actual() == '}') { pos_++; return v; }
        while (true) {
            saltarEspacios();
            string clave = parsearString();
            esperar(':');
            v.objeto[clave] = parsearValor();
            saltarEspacios();
            if (actual() == ',') { pos_++; continue; }
            esperar('}');
            break;
        }
        return v;
    }

    JsonValor parsearArreglo() {
        JsonValor v; v.tipo = JsonValor::ARREGLO;
        esperar('[');
        saltarEspacios();
        if (actual() == ']') { pos_++; return v; }
        while (true) {
            v.arreglo.push_back(parsearValor());
            saltarEspacios();
            if (actual() == ',') { pos_++; continue; }
            esperar(']');
            break;
        }
        return v;
    }

    string parsearString() {
        esperar('"');
        string r;
        while (true) {
            if (pos_ >= s_.size()) throw runtime_error("string sin cerrar");
            char c = s_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= s_.size()) throw runtime_error("escape incompleto");
                r += s_[pos_++];
            } else {
                r += c;
            }
        }
        return r;
    }

    JsonValor parsearBool() {
        JsonValor v; v.tipo = JsonValor::BOOLEANO;
        if (s_.compare(pos_, 4, "true") == 0) { v.b = true; pos_ += 4; }
        else if (s_.compare(pos_, 5, "false") == 0) { v.b = false; pos_ += 5; }
        else throw runtime_error("literal booleano invalido");
        return v;
    }

    JsonValor parsearNull() {
        if (s_.compare(pos_, 4, "null") != 0) throw runtime_error("literal null invalido");
        pos_ += 4;
        return JsonValor{};
    }

    JsonValor parsearNumero() {
        size_t inicio = pos_;
        if (actual() == '-') pos_++;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (!isdigit(static_cast<unsigned char>(c)) && c != '.' && c != 'e' && c != 'E'
                && c != '+' && c != '-') break;
            pos_++;
        }
        if (pos_ == inicio) throw runtime_error("numero invalido");
        JsonValor v; v.tipo = JsonValor::NUMERO;
        v.n = stod(s_.substr(inicio, pos_ - inicio));
        return v;
    }

    const string& s_;
    size_t pos_ = 0;
};

} // namespace

void StateStore::guardar(const EstadoControlador& e) const {
    filesystem::path destino(ruta_);
    if (destino.has_parent_path()) {
        filesystem::create_directories(destino.parent_path());
    }

    ostringstream json;
    json << "{\n"
         << "  \"ts_ultimo_dato_procesado\": " << tsOJson(e.tsUltimoDatoProcesado) << ",\n"
         << "  \"ventana_ma_cpu\": " << arregloDoubles(e.ventanaMaCpu) << ",\n"
         << "  \"holt\": {\n"
         << "    \"inicializado\": " << (e.holt.inicializado ? "true" : "false") << ",\n"
         << "    \"nivel\": " << e.holt.nivel << ",\n"
         << "    \"tendencia\": " << e.holt.tendencia << ",\n"
         << "    \"primer_valor\": " << numOJson(e.holt.primerValor) << "\n"
         << "  },\n"
         << "  \"demanda_ultimos_valores\": " << arregloDoubles(e.demanda.ultimosValores) << ",\n"
         << "  \"ts_ultima_subida\": " << tsOJson(e.tsUltimaSubida) << ",\n"
         << "  \"ts_ultima_bajada\": " << tsOJson(e.tsUltimaBajada) << "\n"
         << "}\n";

    const string temporal = ruta_ + ".tmp";
    {
        ofstream out(temporal, ios::trunc);
        if (!out.is_open()) {
            throw runtime_error("no se pudo abrir el archivo temporal de estado: " + temporal);
        }
        out << json.str();
        if (!out) {
            throw runtime_error("fallo al escribir el archivo temporal de estado: " + temporal);
        }
    }
    // rename() es atomico dentro del mismo filesystem: el archivo destino nunca queda
    // a medio escribir, ni con la version vieja ni con la nueva a medias.
    if (rename(temporal.c_str(), ruta_.c_str()) != 0) {
        throw runtime_error("no se pudo renombrar " + temporal + " a " + ruta_);
    }
}

EstadoControlador StateStore::cargar() {
    ultimaCargaValida_ = true;
    ultimoMotivoCarga_.clear();

    ifstream in(ruta_);
    if (!in.is_open()) {
        ultimaCargaValida_ = false;
        ultimoMotivoCarga_ = "no existe archivo de estado en " + ruta_ + ": arranque en frio";
        return EstadoControlador{};
    }

    ostringstream buffer;
    buffer << in.rdbuf();
    const string contenido = buffer.str();

    try {
        JsonValor raiz = JsonParser(contenido).parsear();

        EstadoControlador e;
        if (const auto* v = raiz.buscar("ts_ultimo_dato_procesado")) e.tsUltimoDatoProcesado = v->comoTs();
        if (const auto* v = raiz.buscar("ventana_ma_cpu")) e.ventanaMaCpu = v->comoVectorDoubles();

        if (const auto* h = raiz.buscar("holt")) {
            e.holt.inicializado = h->buscar("inicializado") ? h->buscar("inicializado")->comoBool(false) : false;
            e.holt.nivel = h->buscar("nivel") ? h->buscar("nivel")->comoDouble().value_or(0.0) : 0.0;
            e.holt.tendencia = h->buscar("tendencia") ? h->buscar("tendencia")->comoDouble().value_or(0.0) : 0.0;
            if (const auto* pv = h->buscar("primer_valor")) e.holt.primerValor = pv->comoDouble();
        }

        if (const auto* v = raiz.buscar("demanda_ultimos_valores")) e.demanda.ultimosValores = v->comoVectorDoubles();
        if (const auto* v = raiz.buscar("ts_ultima_subida")) e.tsUltimaSubida = v->comoTs();
        if (const auto* v = raiz.buscar("ts_ultima_bajada")) e.tsUltimaBajada = v->comoTs();

        return e;
    } catch (const exception& ex) {
        // Corrupto: no se propaga el error, se arranca en frio y se deja constancia.
        ultimaCargaValida_ = false;
        ultimoMotivoCarga_ = string("archivo de estado corrupto (") + ex.what() + "): arranque en frio";
        return EstadoControlador{};
    }
}
