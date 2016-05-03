//
//  main.hpp
//  libraptorQ_tester
//
//  Created by András Oravecz on 01/05/16.
//  Copyright © 2016 András Oravecz. All rights reserved.
//

#ifndef main_h
#define main_h

#ifdef USING_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif	//using_clang
static std::mutex global_mtx;
#ifdef USING_CLANG
#pragma clang diagnostic pop
#endif	//using_clang

typedef std::vector<uint32_t> VECTOR_uint32;
typedef std::vector<uint16_t> VECTOR_uint16;
typedef std::vector<uint8_t>  VECTOR_uint8;
typedef std::vector<float>    VECTOR_float;

typedef struct timerData {
    uint64_t encodeTime;
    uint64_t decodeTime;
} timerData;

typedef struct resultDataFragment {
    uint32_t  subsymbol_count;
    uint32_t  symbol_size;
    uint32_t  repair_package_count;
    float     dropped_data_percentage;
    timerData times;
} resultDataFragment;

typedef struct resultData {
    uint16_t K;
    std::vector<resultDataFragment> datas;
} resultData;

class Timer {
public:

    Timer(){};
    ~Timer(){};
    void start()
    {
        t0 = std::chrono::steady_clock::now();
    }
    uint64_t stop()
    {
        t1 = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(( t1 - t0 ).count());
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> t0, t1;
};

#endif /* main_h */
