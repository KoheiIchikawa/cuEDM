#include <iostream>

#include "dataframe.h"
#include "mpi_master.h"
#include "mpi_worker.h"
#include "nearest_neighbors_cpu.h"
#include "simplex.h"
#include "simplex_cpu.h"
#include "stats.h"
#include "timer.h"

class SimplexMPIMaster : public MPIMaster
{
public:
    SimplexMPIMaster(const std::string &fname, MPI_Comm comm)
        : MPIMaster(comm), current_id(0)
    {
        df.load(fname);
    }
    ~SimplexMPIMaster() {}

protected:
    DataFrame df;
    uint32_t current_id;

    void next_task(nlohmann::json &task) override
    {
        task["id"] = current_id;
        current_id++;
    }

    bool task_left() const override { return current_id < df.columns.size(); }

    void task_done(const nlohmann::json &result) override
    {
        std::cout << "Timeseries #" << result["id"] << " best E=" << result["E"]
                  << " rho=" << result["rho"] << std::endl;
    }
};

class SimplexMPIWorker : public MPIWorker
{
public:
    SimplexMPIWorker(const std::string &fname, MPI_Comm comm)
        : MPIWorker(comm), knn(new NearestNeighborsCPU(1, 1, true)),
          simplex(new SimplexCPU(1, 1, true))
    {
        df.load(fname);
    }
    ~SimplexMPIWorker() {}

protected:
    DataFrame df;
    std::unique_ptr<NearestNeighbors> knn;
    std::unique_ptr<Simplex> simplex;

    void do_task(nlohmann::json &result, const nlohmann::json &task) override
    {
        const auto id = task["id"];

        const auto ts = df.columns[id];

        // Split input into two halves
        const auto library = ts.slice(0, ts.size() / 2);
        const auto target = ts.slice(ts.size() / 2);

        std::vector<float> rhos(20);
        std::vector<float> buffer;

        LUT lut;

        for (auto E = 1; E <= 20; E++) {
            knn->compute_lut(lut, library, target, E);
            lut.normalize();

            const auto prediction = simplex->predict(buffer, lut, library, E);
            const auto shifted_target = simplex->shift_target(target, E);

            rhos[E - 1] = corrcoef(prediction, shifted_target);
        }

        const auto it = std::max_element(rhos.begin(), rhos.end());
        const auto max_E = it - rhos.begin() + 1;
        const auto maxRho = *it;

        result["id"] = id;
        result["E"] = max_E;
        result["rho"] = maxRho;
    }
};

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    if (argc < 2) {
        std::cerr << "No input" << std::endl;
        return -1;
    }

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (!rank) {
        SimplexMPIMaster master(argv[1], MPI_COMM_WORLD);
        Timer timer;

        timer.start();
        master.run();
        timer.stop();

        std::cout << "Processed dataset in " << timer.elapsed() << " [ms]"
                  << std::endl;
    } else {
        SimplexMPIWorker worker(argv[1], MPI_COMM_WORLD);

        worker.run();
    }

    MPI_Finalize();
}
