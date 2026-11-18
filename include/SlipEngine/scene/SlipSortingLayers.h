#pragma once

#ifndef SLIP_SORTING_LAYERS_H
#define SLIP_SORTING_LAYERS_H

#include "SlipEngine.h"
#include <string>
#include <vector>

class SLIPENGINE_API SlipSortingLayers
{
public:
    static SlipSortingLayers& Get();

    const std::vector<std::string>& GetLayers() const { return m_Layers; }
    void SetLayers(const std::vector<std::string>& layers);

    void AddLayer(const std::string& name);
    void RemoveLayer(const std::string& name);
    bool RenameLayer(size_t index, const std::string& newName);

    int GetLayerIndex(const std::string& name) const;
    std::string GetLayerName(int index) const;
    bool HasLayer(const std::string& name) const;

    void ResetToDefaults();

private:
    SlipSortingLayers();
    ~SlipSortingLayers() = default;

    std::vector<std::string> m_Layers;
};

#endif // !SLIP_SORTING_LAYERS_H

