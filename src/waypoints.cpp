#include "waypoints.h"

#include "config.h"

#include <fstream>
#include <sstream>

std::vector<Waypoint> g_waypoints;

// One waypoint per line: name|x|y|z|r|g|b|visible
static std::filesystem::path WaypointsFile() {
    return DataDir() / L"waypoints.txt";
}

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
            g_waypoints.push_back(w);
        } catch (...) {
            // Skip broken lines instead of losing the whole file.
        }
    }
}

void SaveWaypoints() {
    std::ofstream f(WaypointsFile(), std::ios::trunc);
    for (const auto& w : g_waypoints) {
        std::string name = w.name;
        for (char& ch : name)
            if (ch == '|' || ch == '\n' || ch == '\r') ch = ' ';
        f << name << '|' << w.pos.x << '|' << w.pos.y << '|' << w.pos.z << '|' << w.color[0] << '|' << w.color[1]
          << '|' << w.color[2] << '|' << (w.visible ? 1 : 0) << '\n';
    }
}
