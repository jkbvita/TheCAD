// SPDX-License-Identifier: LGPL-2.1-or-later
// TheCAD standalone replacement for optional FreeCAD diagnostic facilities.
#pragma once
#include <chrono>
#include <stdexcept>
#include <format>
#include <iostream>
#include <string_view>
#include <utility>
namespace TheCadPlaneGcs {
struct Console {
    template<class... Args> void log(std::string_view text,Args&&... args) const {
        std::clog<<std::vformat(text,std::make_format_args(args...));
    }
    template<class... Args> void warning(std::string_view text,Args&&... args) const { log(text,std::forward<Args>(args)...); }
};
inline Console& console(){static Console instance;return instance;}
struct TimeElapsed {
    std::chrono::steady_clock::time_point time=std::chrono::steady_clock::now();
    static double diffTimeF(const TimeElapsed& a,const TimeElapsed& b){return std::chrono::duration<double>(b.time-a.time).count();}
};
}
