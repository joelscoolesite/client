#include "waypoints.h"

#include "config.h"

#include <fstream>
#include <sstream>

std::vector<Waypoint> g_waypoints;
Settings g_settings;

namespace {

// One waypoint per line: name|x|y|z|r|g|b|visible|world|dimension|always|death
std::filesystem::path WaypointsFile() {
    return DataDir() / L"waypoints.txt";
}

std::filesystem::path SettingsFile() {
    return DataDir() / L"settings.ini";
}

std::string Clean(std::string text) {
    for (char& ch : text)
        if (ch == '|' || ch == '\n' || ch == '\r') ch = ' ';
    return text;
}

} // namespace

void LoadWaypoints() {
    g_waypoints.clear();
    std::ifstream f(WaypointsFile());
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '|')) parts.push_back(part);
        if (parts.size() < 4) continue;

        try {
            Waypoint w;
            w.name = parts[0];
            w.pos = {std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3])};
            if (parts.size() >= 7) {
                for (int i = 0; i < 3; ++i) w.color[i] = std::stof(parts[4 + i]);
            }
            if (parts.size() >= 8) w.visible = parts[7] != "0";
            if (parts.size() >= 9) w.world = parts[8];
            if (parts.size() >= 10) w.dimension = parts[9];
            if (parts.size() >= 11) w.always = parts[10] == "1";
            if (parts.size() >= 12) w.death = parts[11] == "1";
            g_waypoints.push_back(w);
        } catch (...) {
            // Skip broken lines instead of losing the whole file.
        }
    }
}

void SaveWaypoints() {
    std::ofstream f(WaypointsFile(), std::ios::trunc);
    for (const auto& w : g_waypoints) {
        f << Clean(w.name) << '|' << w.pos.x << '|' << w.pos.y << '|' << w.pos.z << '|' << w.color[0] << '|'
          << w.color[1] << '|' << w.color[2] << '|' << (w.visible ? 1 : 0) << '|' << Clean(w.world) << '|'
          << Clean(w.dimension) << '|' << (w.always ? 1 : 0) << '|' << (w.death ? 1 : 0) << '\n';
    }
}

void LoadSettings() {
    std::ifstream f(SettingsFile());
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        const bool on = !value.empty() && value[0] == '1';
        if (key == "showBeam") g_settings.showBeam = on;
        else if (key == "showDistance") g_settings.showDistance = on;
        else if (key == "netherConversion") g_settings.netherConversion = on;
        else if (key == "deathWaypoints") g_settings.deathWaypoints = on;
        else if (key == "maxDeathWaypoints") {
            try {
                g_settings.maxDeathWaypoints = std::stoi(value);
            } catch (...) {
            }
        }
    }
}

void SaveSettings() {
    std::ofstream f(SettingsFile(), std::ios::trunc);
    f << "showBeam=" << g_settings.showBeam << "\n"
      << "showDistance=" << g_settings.showDistance << "\n"
      << "netherConversion=" << g_settings.netherConversion << "\n"
      << "deathWaypoints=" << g_settings.deathWaypoints << "\n"
      << "maxDeathWaypoints=" << g_settings.maxDeathWaypoints << "\n";
}

const char* DimensionLabel(const std::string& dimension) {
    if (dimension == "Overworld") return "Overworld";
    if (dimension == "Nether") return "Nether";
    if (dimension == "TheEnd") return "The End";
    return dimension.empty() ? "Any" : dimension.c_str();
}
