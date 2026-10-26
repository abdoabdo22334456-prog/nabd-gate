#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Nabd {
constexpr int Steps = 16;
constexpr int MaxPatterns = 4;
constexpr int LegacyEditableCount = 20;
constexpr int Version11Count = 69;
// Original IDs are kept stable for projects and automation from version 1.0.
enum Param : uint32_t { Rate = 16, Smooth, Wet, Bypass,
                       ExtraSteps = 20, PatternCount = 68,
                       Playhead = 100, Tempo = 101,
                       RepeatSteps = 128, RepeatEnabled = 192, RepeatSize = 193,
                       GateEnabled = 194, EditableCount = 195 };
constexpr bool isEditable(uint32_t id) { return id<Version11Count || (id>=RepeatSteps && id<EditableCount); }
constexpr uint32_t repeatParam(int pattern,int step) { return RepeatSteps+pattern*Steps+step; }
constexpr uint32_t stepParam(int pattern, int step) {
    return pattern==0 ? uint32_t(step) : uint32_t(ExtraSteps+(pattern-1)*Steps+step);
}
constexpr std::array<double, 6> StepRates {1., 2., 4., 8., 3., 6.};
constexpr const char* RateNames[] {"1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T"};
constexpr std::array<double,6> RepeatBeats {1.,0.5,0.25,0.125,0.0625,1./6.};
constexpr const char* RepeatNames[] {"1/4", "1/8", "1/16", "1/32", "1/64", "1/16 T"};
constexpr std::array<double, LegacyEditableCount> LegacyDefaults {
    1,1,0,1, 0,0,1,0, 1,0,1,0, 0,1,0,0, 0.4,0.15,1,0
};
inline const std::array<double, EditableCount> Defaults = [] {
    std::array<double, EditableCount> result {};
    for (int i=0;i<LegacyEditableCount;++i) result[i]=LegacyDefaults[i];
    for (int row=1;row<MaxPatterns;++row)
        for (int step=0;step<Steps;++step)
            result[stepParam(row,step)]=row==1 ? (step%4>=2 ? 1. : 0.) : LegacyDefaults[(step+row*3)%16];
    result[PatternCount]=1./3.; // New instances start with two sequential patterns.
    for (int row=0;row<MaxPatterns;++row) for (int step=0;step<Steps;++step)
        result[repeatParam(row,step)]=step%4==3 ? 1. : 0.;
    result[RepeatSize]=0.6;
    result[RepeatEnabled]=0.; result[GateEnabled]=1.;
    return result;
}();
inline double normalized(double v) { return std::isfinite(v) ? std::clamp(v, 0., 1.) : 0.; }
inline int rateIndex(double v) { return std::clamp(int(std::lround(normalized(v)*5.)), 0, 5); }
inline int patternCount(double value) { return std::clamp(int(std::lround(normalized(value)*3.))+1,1,MaxPatterns); }
inline int sequenceAt(double quarterNotes, double rate, int patterns) {
    if (!std::isfinite(quarterNotes)) return 0;
    // Reduce in floating point first: avoids integer overflow in long sessions.
    double index = std::floor(quarterNotes*rate + 1.e-9);
    const int length=Steps*std::clamp(patterns,1,MaxPatterns);
    int step = int(std::fmod(index, double(length)));
    return step < 0 ? step+length : step;
}
inline int stepAt(double quarterNotes, double rate) { return sequenceAt(quarterNotes,rate,1); }
struct Ramp {
    double value = 1., target = 1., increment = 0.;
    int remaining = 0;
    void reset(double v = 1.) { value=target=v; increment=0.; remaining=0; }
    double tick(double next, int samples) {
        if (next != target) {
            target=next; remaining=samples;
            increment=samples>0 ? (target-value)/samples : 0.;
        }
        if (samples<=0) { value=target; remaining=0; }
        else if (remaining>0) {
            value += increment;
            if (--remaining==0) value=target;
        }
        return value;
    }
};
struct GateEngine {
    struct Frame { int position; double gate,amount; };
    std::array<double, EditableCount> params = Defaults;
    double sampleRate = 48000.;
    Ramp gate, wet, enabled;
    void prepare(double sr) {
        sampleRate = sr>0. && std::isfinite(sr) ? sr : 48000.;
        gate.reset(1.); wet.reset(params[Wet]); enabled.reset(params[Bypass]>=0.5 ? 0. : 1.);
    }
    void set(uint32_t id, double v) {
        if (isEditable(id)) params[id]=normalized(v);
    }
    Frame advance(double ppq, bool playing) {
        const int position=sequenceAt(ppq,StepRates[rateIndex(params[Rate])],patternCount(params[PatternCount]));
        const double target=playing && params[GateEnabled]>=0.5 ? (params[stepParam(position/Steps,position%Steps)]>=0.5 ? 1. : 0.) : 1.;
        const double g=gate.tick(target, int(std::lround(params[Smooth]*0.020*sampleRate)));
        const int transition=std::max(1, int(sampleRate*0.005));
        const double mix=wet.tick(params[Wet], transition);
        const double active=enabled.tick(params[Bypass]>=0.5 ? 0. : 1., transition);
        return {position,g,active*mix};
    }
    double gain(double ppq,bool playing) { const auto f=advance(ppq,playing); return 1.+f.amount*(f.gate-1.); }
};
} // namespace Nabd
