#pragma once
#include <cmath>
#include <iostream>
#include <sstream>

// Mini test runner: cada CHECK imprime PASS/FAIL y acumula fallos.
inline int& fallos() { static int f = 0; return f; }

#define CHECK(cond, nombre) \
    do { \
        std::ostringstream n_; n_ << nombre; \
        if (cond) { std::cout << "PASS: " << n_.str() << "\n"; } \
        else { std::cout << "FAIL: " << n_.str() << "  (" #cond ")\n"; fallos()++; } \
    } while (0)

#define CHECK_NEAR(obtenido, esperado, tol, nombre) \
    do { \
        double o_ = (obtenido), e_ = (esperado); \
        if (std::fabs(o_ - e_) <= (tol)) { std::cout << "PASS: " << nombre << " (" << o_ << ")\n"; } \
        else { std::cout << "FAIL: " << nombre << "  esperado=" << e_ << " obtenido=" << o_ << "\n"; fallos()++; } \
    } while (0)
