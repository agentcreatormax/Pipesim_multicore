#include "pipesim.h"
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <exception>

int main(int argc, char** argv) {

    if (argc != 4) {

        std::cerr << "usage: ./pipesim program.pisa cache_config.json predictor_number\n";

        return 1;

    }

    pipesim::Machine machine;

    std::string error;

    if (!machine.LoadConfiguration(argv[2], &error)) {

        std::cerr << "error: " << error << "\n";

        return 1;

    }

    if (!machine.LoadProgram(argv[1], &error)) {

        std::cerr << "error: " << error << "\n";

        return 1;

    }

    std::unique_ptr<pipesim::Processor> processor;

    try {

        processor = pipesim::BuildDesign(&machine);

        const int predictor_number = std::stoi(argv[3]);

        if (predictor_number < 0 || predictor_number > 2) {

            throw std::runtime_error("predictor number must be 0, 1, or 2");

        }

        processor->SelectPredictor(static_cast<std::size_t>(predictor_number));

        processor->Reset();

    }

    catch (const std::exception& exception) {

        std::cerr << "error: " << exception.what() << "\n";

        return 1;

    }

    std::string line;

    while (std::getline(std::cin, line)) {

        std::istringstream command(line);

        std::string name;

        command >> name;

        if (name.empty()) continue;

        if (name == "q") break;

        if (name == "n") {

            int count = 1;

            if (command >> count) {

                std::string extra;

                if (count < 0 || command >> extra) {

                    std::cout << "ERROR invalid cycle count\n";

                    continue;

                }

            }

            int executed = 0;

            try {

                while (executed < count && !processor->IsHalted()) {

                    processor->Clock();

                    ++executed;

                }

            }

            catch (const std::exception& exception) {

                std::cerr << "error: " << exception.what() << "\n";

                return 1;

            }

            std::cout << "CYCLES " << executed << "\nSTATUS "                 << (processor->IsHalted() ? "HALTED" : "RUNNING") << "\n";

        }

        else if (name == "p") {

            machine.PrintState(std::cout);

        }

        else if (name == "pipe") {

            processor->PrintPipeline(std::cout);

        }

        else if (name == "cache") {

            machine.PrintCacheState(std::cout);

        }

        else if (name == "graph") {

            processor->PrintStructure(std::cout);

        }

        else if (name == "s") {

            processor->PrintStatistics(std::cout);

        }

        else if (name == "reset") {

            processor->Reset();

            std::cout << "RESET OK\n";

        }

        else {

            std::cout << "ERROR unknown command\n";

        }

    }

    return 0;

}

