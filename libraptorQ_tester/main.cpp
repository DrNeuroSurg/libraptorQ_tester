//
//  main.cpp
//  libraptorQ_tester
//
//  Created by András Oravecz on 30/04/16.
//  Copyright © 2016 András Oravecz. All rights reserved.
//

#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include "RaptorQ.hpp"
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "main.hpp"

using namespace std;

timerData make_test (
                     std::mt19937_64 &rnd,
                     uint32_t        test_data_size,
                     uint32_t        subsymbol_count,
                     uint32_t        symbol_size,
                     float           dropped_data_percentage,
                     uint32_t        repair_package_count
                     );

void bench (
                uint16_t                    *K_idx,
                std::vector<resultData>     *resultData,
                VECTOR_uint32               *par_subsymbol_count,
                VECTOR_uint32               *par_symbol_size,
                VECTOR_float                *par_repair_package_percentage,
                VECTOR_float                *par_dropped_data_percentage
){

    std::mt19937_64 rnd;
    std::ifstream rand("/dev/random");
    uint64_t seed = 0;
    rand.read (reinterpret_cast<char *> (&seed), sizeof(seed));
    rand.close ();
    rnd.seed (seed);

    // For multiple thread executing we need to copy the parameters
    VECTOR_uint32         subsymbol_count=(*par_subsymbol_count);
    VECTOR_uint32         symbol_size=(*par_symbol_size);
    VECTOR_float          repair_package_percentage=(*par_repair_package_percentage);
    VECTOR_float          dropped_data_percentage=(*par_dropped_data_percentage);

    while (true) {

        global_mtx.lock();
        auto idx = *K_idx;

        if (idx >= (*resultData).size()) {
            global_mtx.unlock();
            return;
        } else {
            (*K_idx)++;
            global_mtx.unlock();
        }

        if (idx >= (*resultData).size())
            return;

        auto size = (*resultData)[idx].K;

        (*resultData)[idx].datas.reserve(subsymbol_count.capacity() * symbol_size.capacity() * repair_package_percentage.capacity() * dropped_data_percentage.capacity());

        for (auto symbol_size_iterator : symbol_size) {
            for (auto subsymbol_count_iterator : subsymbol_count) {
                for (auto repair_package_count_percentage : repair_package_percentage) {
                    for (auto dropped_data_percentage_iterator : dropped_data_percentage) {

                        cout << "K: " << size << " subsymbol_c: " << subsymbol_count_iterator << " symbol_s:" << symbol_size_iterator << " repair_p_c:" << repair_package_count_percentage * 100 << " dropped_d_per:" << dropped_data_percentage_iterator << "\n";

                        timerData localTimes = make_test (
                                                          rnd,
                                                          size,
                                                          subsymbol_count_iterator,
                                                          symbol_size_iterator,
                                                          dropped_data_percentage_iterator,
                                                          (uint32_t)(repair_package_count_percentage*100)
                                                          );
                        cout << " encodeTime: " << localTimes.encodeTime << " decodeTime:" << localTimes.decodeTime << "\n";

                        (*resultData)[idx].datas.push_back({
                            subsymbol_count_iterator,
                            symbol_size_iterator,
                            static_cast<uint32_t>(repair_package_count_percentage*100),
                            dropped_data_percentage_iterator,
                            localTimes
                        });
                    }
                }
            }
        }
    }
}

// returns number of microseconds for encoding and decoding

timerData make_test (
                        std::mt19937_64 &rnd,
                        uint32_t        test_data_size,
                        uint32_t        subsymbol_count,
                        uint32_t        symbol_size,
                        float           dropped_data_percentage,
                        uint32_t        repair_package_count_percentage
){
    Timer timer;
    timerData times={0,0};

    VECTOR_uint32 testData;

    std::vector<std::pair<uint32_t, VECTOR_uint32>> encoded_data;
    
    //initialize vector
    std::uniform_int_distribution<uint32_t> distr(0, ~static_cast<uint32_t>(0));
    testData.reserve (test_data_size);
    for (uint32_t i = 0; i < test_data_size; ++i)
        testData.push_back (distr(rnd));

    auto test_data_iterator = testData.begin();
    RaptorQ::Encoder<VECTOR_uint32::iterator, VECTOR_uint32::iterator> raptorQEncoder (
                                                                                       test_data_iterator,
                                                                                       testData.end(),
                                                                                       subsymbol_count,
                                                                                       symbol_size,
                                                                                       1073741824
                                                                                       );
    
    timer.start();
    raptorQEncoder.precompute(1, false);
    times.encodeTime = timer.stop();
    
    if (times.encodeTime == 0)
        return times;
    
    for (auto block : raptorQEncoder) {

        uint32_t dropped = 0;
        uint32_t must_drop = (uint32_t)(block.block_size()/sizeof(block.begin_source())/sizeof(block.begin_source())*dropped_data_percentage/100);

        int32_t repair = (int32_t)(block.block_size()/sizeof(block.begin_source())/sizeof(block.begin_source())*repair_package_count_percentage/100);

        uint32_t in = 0;

        vector<bool> mustdrop_list;
        mustdrop_list.reserve(block.block_size()/sizeof(block.begin_source())/sizeof(block.begin_source()));
        while(in!=block.block_size()){
            mustdrop_list.push_back(false);
            in++;
        }

        in=0;
        uniform_real_distribution<float> drop (0, block.block_size()/sizeof(block.begin_source())/sizeof(block.begin_source()));
        while(must_drop){
            uint32_t random_int = (int32_t)drop(rnd);
            if(mustdrop_list[random_int]!=true){
                mustdrop_list[random_int]=true;
                must_drop--;
                in++;
            }
        }

        in=0;

        for (auto symbol_iterator = block.begin_source(); symbol_iterator != block.end_source(); ++symbol_iterator) {
            if (mustdrop_list[in]==true) {
                dropped++;
                in++;
                continue;
            }
            in++;
            VECTOR_uint32 source_sym;
            source_sym.reserve (symbol_size / 4);
            source_sym.insert (source_sym.begin(), symbol_size / 4, 0);
            auto it = source_sym.begin();
            (*symbol_iterator) (it, source_sym.end());
            encoded_data.emplace_back ((*symbol_iterator).id(), std::move(source_sym));
        }

        cout << "dropped:" << dropped << "\n";
        
        auto repair_symbol_iterator = block.begin_repair();
        for (; repair >= 0 && repair_symbol_iterator != block.end_repair (block.max_repair());
             ++repair_symbol_iterator) {
            // repair symbols can be lost, too
            /*float dropped = drop (rnd);
            if (dropped <= dropped_data_percentage) {
                continue;
            }*/
            --repair;
            VECTOR_uint32 repair_sym;
            repair_sym.reserve (symbol_size / 4);
            repair_sym.insert (repair_sym.begin(), symbol_size / 4, 0);
            auto it = repair_sym.begin();
            (*repair_symbol_iterator) (it, repair_sym.end());
            encoded_data.emplace_back ((*repair_symbol_iterator).id(), std::move(repair_sym));
        }
        
        // we dropped waaaay too many symbols! how much are you planning to
        // lose, again???
        
        if (repair_symbol_iterator == block.end_repair (block.max_repair())) {
            cout << "Maybe losing " << dropped_data_percentage << "% is too much?\n";
            return {0,0};
        }
        
    }

    auto oti_scheme = raptorQEncoder.OTI_Scheme_Specific();
    auto oti_common = raptorQEncoder.OTI_Common();
    
    RaptorQ::Decoder<VECTOR_uint32::iterator, VECTOR_uint32::iterator> raptorQDecoder (
                                                                                           oti_common,
                                                                                           oti_scheme
                                                                                       );
    
    VECTOR_uint32 receivedData;
    receivedData.reserve (test_data_size);
    for (uint32_t i = 0; i < test_data_size; ++i)
        receivedData.push_back (0);
    
    for (size_t i = 0; i < encoded_data.size(); ++i) {
        auto iterator = encoded_data[i].second.begin();
        raptorQDecoder.add_symbol (iterator, encoded_data[i].second.end(), encoded_data[i].first);
    }
    
    auto re_it_begin = receivedData.begin();
    auto re_it_end   = receivedData.end();
    timer.start();
    auto decodedData = raptorQDecoder.decode(re_it_begin, re_it_end, 0);
    times.decodeTime = timer.stop();
    
    if (decodedData != test_data_size) {
        cout << "NOPE: "<< test_data_size << " - " << dropped_data_percentage << " - " <<
        /*static_cast<int> (repair_package_count) <<*/ "\n";
        return {0,0};
    }
    for (uint32_t i = 0; i < test_data_size; ++i) {
        if (testData[i] != receivedData[i]) {
            cout << "FAILED, but we though otherwise! " << test_data_size << " - "
            << dropped_data_percentage << " - " <<
            /*static_cast<int> (repair_package_count) <<*/ "\n";
            return {0,0};
        }
    }

    return times;
}

void save (
           std::string &filename,
           std::vector<resultData> &resultData
){

    std::ofstream myfile;
    myfile.open (filename, std::ios::out | std::ios::trunc | std::ios::binary);

    if (myfile.is_open()) {

        myfile << "% K, subsymbol_count, symbol_size, repair_package_count, dropped_data_percentage, encodeTime, decodeTime\n\n";
        myfile << "result = [\n";

        for(auto resultDataElement : resultData){
            uint32_t K = resultDataElement.K;
            for(auto el_it : resultDataElement.datas){
                myfile
                    << K                                << " "
                    << el_it.subsymbol_count            << " "
                    << el_it.symbol_size                << " "
                    << el_it.repair_package_count       << " "
                    << el_it.dropped_data_percentage    << " "
                    << el_it.times.encodeTime           << " "
                    << el_it.times.decodeTime           << ",\n";
            }
        }

        myfile << "];";

        myfile.close();
        std::cout << "Saved file\n";
    } else {
        std::cout << "Can't save!\n";
    }
}

int main (
      int argc,
      char **argv
){
    // get the amount of threads to use
    uint8_t threads = 0;

    char *end_ptr = nullptr;
    switch (argc) {
        case 2:
            // one argument: benchmark: the number of threads.
            threads = static_cast<uint32_t> (strtol(argv[1], &end_ptr, 10));
            if ((end_ptr != nullptr && end_ptr != argv[1] + strlen(argv[1]))) {
                // some problem. print help and exit
                cout << "Usage:\t\t" << argv[0] << " [threads]\n";
                return 1;
            }
            if (threads == 0)
                threads = std::thread::hardware_concurrency()-1;
            threads = (threads) ? threads : 1;
            break;
            // else fallthrough
#ifdef USING_CLANG
            [[clang::fallthrough]];
#endif
        default:
            cout << "libRaptorQ tests\n";
            cout << "\tUsage:\t\t" << argv[0] << " [threads]\n";
            return 0;
    }
    

    uint16_t K_index = 0;
    
    std::vector<std::thread> t;
    t.reserve (threads + 1);
    
    std::vector<resultData> resultData;
    resultData.reserve(477);
    for(uint32_t i=0;i<477;i++)
        resultData.push_back({
            static_cast<uint16_t>(RaptorQ::Impl::K_padded[i])
        });

    /*resultData.push_back({
        static_cast<uint16_t>(RaptorQ::Impl::K_padded[476])
    });*/

    /// TEST PARAMETERS
    std::string     filename                  = "result.txt";
    VECTOR_uint32   subsymbol_count           = {1024};
    VECTOR_uint32   symbol_size               = {1024};
    VECTOR_float    repair_package_percentage = {40};
    VECTOR_float    dropped_data_percentage   = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    for (uint8_t i = 0; i < threads; ++i)
        t.emplace_back (bench, &K_index, &resultData, &subsymbol_count, &symbol_size, &repair_package_percentage, &dropped_data_percentage);
    
    while (K_index < resultData.size()) {
        std::this_thread::sleep_for (std::chrono::seconds(1));
        cout << "Done: " << floor((float)K_index / resultData.size() * 100) << "%" << "\n";
    }
    
    for (uint8_t i = 0; i < threads; ++i)
        t[i].join();

    save(filename, resultData);
    
    return 0;
}
