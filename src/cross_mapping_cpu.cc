#include <iostream>

#include "cross_mapping_cpu.h"
#include "stats.h"

void CrossMappingCPU::predict(std::vector<float> &rhos,
                              const Timeseries &library,
                              const std::vector<Timeseries> &targets,
                              const std::vector<uint32_t> &optimal_E)
{
    // Compute lookup tables for library timeseries
    for (auto E = 1; E <= E_max; E++) {
        knn->compute_lut(luts[E - 1], library, library, E);
        luts[E - 1].normalize();
    }

    // Compute Simplex projection from the library to every target
    for (auto i = 0; i < targets.size(); i++) {
        const Timeseries target = targets[i];
        Timeseries prediction;
        Timeseries shifted_target;
        const uint32_t E = optimal_E[i];

        simplex->predict(prediction, luts[E - 1], target, E);
        simplex->shift_target(shifted_target, target, E);

        corrcoef(prediction, shifted_target);
    }
}
