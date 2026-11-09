#pragma once
#include <QString>

// Sprawdzian srodowiska: ktore elementy GStreamera faktycznie sie laduja.
namespace EnvCheck {
struct Result {
    QString report;
    bool requiredOk = true;
};
Result run();
bool hasElement(const char* name);
}
