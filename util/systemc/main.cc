/*
 * Copyright (c) 2014 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Andrew Bardsley
 */

/**
 * @file
 *
 *  Example top level file for SystemC integration with C++-only
 *  instantiation.
 *
 *  Build with something like:
 *
 *      scons --without-python build/ARM/libgem5_opt.so
 *
 *      g++ -std=c++0x -Ibuild/ARM -Isrc -DTRACING_ON \
 *          -o gem5cxx.opt -Lbuild/ARM -lgem5_opt \
 *          src/sim/sc_main_cxx.cc src/sim/cxx_stats.cc \
 *          src/sim/sc_module.cc src/sim/sc_logger.cc
 */

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <systemc>

#include "base/statistics.hh"
#include "base/str.hh"
#include "base/trace.hh"
#include "cpu/base.hh"
#include "sim/cxx_config_ini.hh"
#include "sim/cxx_manager.hh"
#include "sim/init_signals.hh"
#include "sim/serialize.hh"
#include "sim/simulate.hh"
#include "sim/stat_control.hh"
#include "sim/system.hh"
#include "sc_logger.hh"
#include "sc_module.hh"
#include "stats.hh"

void
usage(const std::string &prog_name)
{
    std::cerr << "Usage: " << prog_name << (
        " <config_file.ini> [ <option> ]\n\n"
        "OPTIONS:\n"
        "    -p <object> <param> <value>  -- set a parameter\n"
        "    -v <object> <param> <values> -- set a vector parameter from"
        " a comma\n"
        "                                    separated values string\n"
        "    -d <flag>                    -- set a debug flag (-<flag>\n"
        "                                    clear a flag)\n"
        "    -s <dir> <ticks>             -- save checkpoint to dir after"
        " the given\n"
        "                                    number of ticks\n"
        "    -r <dir>                     -- restore checkpoint to dir\n"
        "    -c <from> <to> <ticks>       -- switch from cpu 'from' to cpu"
        " 'to' after\n"
        "                                    the given number of ticks\n"
        "\n"
        );

    std::exit(EXIT_FAILURE);
}

class SimControl : public Gem5SystemC::Module
{
  protected:
    int argc;
    char **argv;
    CxxConfigManager *config_manager;
    Gem5SystemC::Logger logger;

    bool checkpoint_restore;
    bool checkpoint_save;
    bool switch_cpus;
    std::string checkpoint_dir;
    std::string from_cpu;
    std::string to_cpu;
    Tick pre_run_time;
    Tick pre_switch_time;

  public:
    SC_HAS_PROCESS(SimControl);

    SimControl(sc_core::sc_module_name name, int argc_, char **argv_);

    void before_end_of_elaboration();

    void run();
};

SimControl::SimControl(sc_core::sc_module_name name,
    int argc_, char **argv_) :
    Gem5SystemC::Module(name),
    argc(argc_),
    argv(argv_)
{
    SC_THREAD(run);

    std::string prog_name(argv[0]);
    unsigned int arg_ptr = 1;

    if (argc == 1)
        usage(prog_name);

    cxxConfigInit();

    Trace::setDebugLogger(&logger);

    Gem5SystemC::setTickFrequency();
    sc_core::sc_set_time_resolution(1, sc_core::SC_PS);

    Gem5SystemC::Module::setupEventQueues(*this);
    initSignals();

    Stats::initSimStats();
    Stats::registerHandlers(CxxConfig::statsReset, CxxConfig::statsDump);

    Trace::enabled = true;
    setDebugFlag("Terminal");

    checkpoint_restore = false;
    checkpoint_save = false;
    switch_cpus = false;
    checkpoint_dir = "";
    from_cpu = "";
    to_cpu = "";
    pre_run_time = 1000000;
    pre_switch_time = 1000000;

    const std::string config_file(argv[arg_ptr]);

    CxxConfigFileBase *conf = new CxxIniFile();

    if (!conf->load(config_file.c_str())) {
        std::cerr << "Can't open config file: " << config_file << '\n';
        std::exit(EXIT_FAILURE);
    }
    arg_ptr++;

    config_manager = new CxxConfigManager(*conf);

    try {
        while (arg_ptr < argc) {
            std::string option(argv[arg_ptr]);
            arg_ptr++;
            unsigned num_args = argc - arg_ptr;

            if (option == "-p") {
                if (num_args < 3)
                    usage(prog_name);
                config_manager->setParam(argv[arg_ptr], argv[arg_ptr + 1],
                    argv[arg_ptr + 2]);
                arg_ptr += 3;
            } else if (option == "-v") {
                std::vector<std::string> values;

                if (num_args < 3)
                    usage(prog_name);
                tokenize(values, argv[2], ',');
                config_manager->setParamVector(argv[arg_ptr],
                    argv[arg_ptr], values);
                arg_ptr += 3;
            } else if (option == "-d") {
                if (num_args < 1)
                    usage(prog_name);
                if (argv[arg_ptr][0] == '-')
                    clearDebugFlag(argv[arg_ptr] + 1);
                else
                    setDebugFlag(argv[arg_ptr]);
                arg_ptr++;
            } else if (option == "-r") {
                if (num_args < 1)
                    usage(prog_name);
                checkpoint_dir = argv[arg_ptr];
                checkpoint_restore = true;
                arg_ptr++;
            } else if (option == "-s") {
                if (num_args < 2)
                    usage(prog_name);
                checkpoint_dir = argv[arg_ptr];
                std::istringstream(argv[arg_ptr + 1]) >> pre_run_time;
                checkpoint_save = true;
                arg_ptr += 2;
            } else if (option == "-c") {
                if (num_args < 3)
                    usage(prog_name);
                switch_cpus = true;
                from_cpu = argv[arg_ptr];
                to_cpu = argv[arg_ptr + 1];
                std::istringstream(argv[arg_ptr + 2]) >> pre_switch_time;
                arg_ptr += 3;
            } else {
                usage(prog_name);
            }
        }
    } catch (CxxConfigManager::Exception &e) {
        std::cerr << e.name << ": " << e.message << "\n";
        std::exit(EXIT_FAILURE);
    }

    CxxConfig::statsEnable();
    getEventQueue(0)->dump();

    try {
        config_manager->instantiate();
    } catch (CxxConfigManager::Exception &e) {
        std::cerr << "Config problem in sim object " << e.name
            << ": " << e.message << "\n";

        std::exit(EXIT_FAILURE);
    }
}

void SimControl::before_end_of_elaboration()
{
    if (!checkpoint_restore) {
        try {
            config_manager->initState();
            config_manager->startup();
        } catch (CxxConfigManager::Exception &e) {
            std::cerr << "Config problem in sim object " << e.name
                << ": " << e.message << "\n";

            std::exit(EXIT_FAILURE);
        }
    }
}

void SimControl::run()
{
    GlobalSimLoopExitEvent *exit_event = NULL;

    if (checkpoint_save) {
        exit_event = simulate(pre_run_time);

        DrainManager drain_manager;

        unsigned int drain_count = 1;
        do {
            drain_count = config_manager->drain(&drain_manager);

            std::cerr << "Draining " << drain_count << '\n';

            if (drain_count > 0) {
                drain_manager.setCount(drain_count);
                exit_event = simulate();
            }
        } while (drain_count > 0);

        std::cerr << "Simulation stop at tick " << curTick()
            << ", cause: " << exit_event->getCause() << '\n';

        std::cerr << "Checkpointing\n";

        /* FIXME, this should really be serialising just for
         *  config_manager rather than using serializeAll's ugly
         *  SimObject static object list */
        Serializable::serializeAll(checkpoint_dir);

        std::cerr << "Completed checkpoint\n";

        config_manager->drainResume();
    }

    if (checkpoint_restore) {
        std::cerr << "Restoring checkpoint\n";

        Checkpoint *checkpoint = new Checkpoint(checkpoint_dir,
            config_manager->getSimObjectResolver());

        Serializable::unserializeGlobals(checkpoint);

        /* gem5 time can have changed, so lets wait until SystemC
         *  catches up */
        Tick systemc_time = sc_core::sc_time_stamp().value();
        if (curTick() > systemc_time) {
            Tick wait_period = curTick() - systemc_time;

            std::cerr << "Waiting for " << wait_period << "ps for"
                " SystemC to catch up to gem5\n";
            wait(sc_core::sc_time(wait_period, sc_core::SC_PS));
        }

        config_manager->loadState(checkpoint);

        config_manager->drainResume();

        std::cerr << "Restored from checkpoint\n";
    }

    if (switch_cpus) {
        exit_event = simulate(pre_switch_time);

        std::cerr << "Switching CPU\n";

        /* Assume the system is called system */
        System &system = config_manager->getObject<System>("system");
        BaseCPU &old_cpu = config_manager->getObject<BaseCPU>(from_cpu);
        BaseCPU &new_cpu = config_manager->getObject<BaseCPU>(to_cpu);

        DrainManager drain_manager;
        unsigned int drain_count = 1;
        do {
            drain_count = config_manager->drain(&drain_manager);

            std::cerr << "Draining " << drain_count << '\n';

            if (drain_count > 0) {
                drain_manager.setCount(drain_count);
                exit_event = simulate();
            }
        } while (drain_count > 0);

        old_cpu.switchOut();
        system.setMemoryMode(Enums::timing);
        new_cpu.takeOverFrom(&old_cpu);
        config_manager->drainResume();

        std::cerr << "Switched CPU\n";
    }

    exit_event = simulate();

    std::cerr << "Exit at tick " << curTick()
        << ", cause: " << exit_event->getCause() << '\n';

    getEventQueue(0)->dump();

#if TRY_CLEAN_DELETE
    config_manager->deleteObjects();
#endif
}

int
sc_main(int argc, char **argv)
{
    SimControl sim_control("gem5", argc, argv);

    sc_core::sc_start();

    return EXIT_SUCCESS;
}
