#include "TerrainHeightMap.h"

#include <algorithm>
#include <fstream>

namespace
{
    constexpr float ORIGIN_X = -156.71f;
    constexpr float ORIGIN_Y = -14.43f;
    constexpr float ORIGIN_Z = -255.0f;

    constexpr float SCALE_X = 533.2781f / 4096.0f;
    constexpr float SCALE_Y = 29.68098f;
    constexpr float SCALE_Z = 534.9254f / 4096.0f;
}

TerrainHeightMap g_terrainHeightMap;

bool TerrainHeightMap::Load(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return false;

    const size_t pixelCount = static_cast<size_t>(WIDTH) * LENGTH;
    std::vector<unsigned short> raw(pixelCount);
    file.read(reinterpret_cast<char*>(raw.data()), pixelCount * sizeof(unsigned short));
    if (file.gcount() != static_cast<std::streamsize>(pixelCount * sizeof(unsigned short)))
        return false;

    // Match the client's CHeightMapImage constructor: vertically flip the RAW rows.
    m_heights.resize(pixelCount);
    for (int z = 0; z < LENGTH; ++z)
    {
        const int flippedZ = LENGTH - 1 - z;
        for (int x = 0; x < WIDTH; ++x)
            m_heights[x + flippedZ * WIDTH] = raw[x + z * WIDTH];
    }

    m_loaded = true;
    return true;
}

float TerrainHeightMap::GetWorldHeight(float worldX, float worldZ) const
{
    if (!m_loaded) return ORIGIN_Y;

    const float fx = (worldX - ORIGIN_X) / SCALE_X;
    const float fz = (worldZ - ORIGIN_Z) / SCALE_Z;
    if (fx < 0.0f || fz < 0.0f || fx >= WIDTH - 1 || fz >= LENGTH - 1)
        return ORIGIN_Y - 4.0f;

    const int x = static_cast<int>(fx);
    const int z = static_cast<int>(fz);
    const float tx = fx - x;
    const float tz = fz - z;

    auto sample = [this](int sampleX, int sampleZ)
    {
        return static_cast<float>(m_heights[sampleX + sampleZ * WIDTH]);
    };

    float bottomLeft = sample(x, z);
    float bottomRight = sample(x + 1, z);
    float topLeft = sample(x, z + 1);
    float topRight = sample(x + 1, z + 1);

    // This is the same triangle correction used by CHeightMapImage::GetHeight.
    const bool reverseQuad = (z % 2) != 0;
    if (reverseQuad)
    {
        if (tz >= tx) bottomRight = bottomLeft + (topRight - topLeft);
        else topLeft = topRight + (bottomLeft - bottomRight);
    }
    else
    {
        if (tz < (1.0f - tx)) topRight = topLeft + (bottomRight - bottomLeft);
        else bottomLeft = topLeft + (bottomRight - topRight);
    }

    const float top = topLeft * (1.0f - tx) + topRight * tx;
    const float bottom = bottomLeft * (1.0f - tx) + bottomRight * tx;
    const float height = bottom * (1.0f - tz) + top * tz;
    return ORIGIN_Y + (height / 65535.0f) * SCALE_Y;
}
