#pragma once

#include <string>
#include <vector>

class TerrainHeightMap
{
public:
    static constexpr int WIDTH = 4097;
    static constexpr int LENGTH = 4097;

    bool Load(const std::string& filePath);
    float GetWorldHeight(float worldX, float worldZ) const;
    bool IsLoaded() const { return m_loaded; }

private:
    std::vector<unsigned short> m_heights;
    bool m_loaded = false;
};

extern TerrainHeightMap g_terrainHeightMap;
