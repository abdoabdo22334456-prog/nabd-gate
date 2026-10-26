#pragma once
#include "GateEngine.h"
#include <vector>

namespace Nabd {
// All recording and snapshot storage is allocated by prepare(), outside process().
class StutterEngine {
public:
    using Stereo=std::array<double,2>;
    void prepare(double sr) {
        sampleRate=sr;
        capacity=std::max<size_t>(4,size_t(std::ceil(sr*8.))+2);
        history.resize(capacity); slice.resize(capacity); reset();
    }
    void reset() {
        write=available=read=length=age=0;
        captured=pending=requested=wasPlaying=clockValid=false;
        lastCell=-1; expectedPpq=0.; blend.reset(0.);
    }
    void beginBlock(double ppq,double delta,bool playing,int samples) {
        if (samples<=0) return;
        const bool jump=clockValid && std::abs(ppq-expectedPpq)>std::max(1.e-7,std::abs(delta)*0.25);
        if (!playing || !wasPlaying || jump) reset();
        wasPlaying=playing; clockValid=playing;
        expectedPpq=ppq+double(samples)*delta;
    }
    Stereo process(Stereo input,int cell,bool active,bool playing,double bpm,int sizeIndex,double smooth) {
        if (!playing || capacity==0) return input;
        const double wanted=sampleRate*60./std::max(0.001,bpm)*RepeatBeats[std::clamp(sizeIndex,0,5)];
        const size_t frames=size_t(std::clamp(std::round(wanted),2.,double(capacity-2)));
        const int smoothing=int(std::lround(normalized(smooth)*0.020*sampleRate));
        if (active && (!requested || cell!=lastCell || frames!=length || age>=capacity-2)) pending=true;
        if (!active) pending=false;
        if (pending && available>=frames) {
            const size_t start=(write+capacity-frames)%capacity;
            const size_t first=std::min(frames,capacity-start);
            std::copy_n(history.data()+start,first,slice.data());
            if (first<frames) std::copy_n(history.data(),frames-first,slice.data()+first);
            length=frames; read=age=0; captured=true; pending=false;
        }
        const double weight=blend.tick(active && captured ? 1. : 0.,smoothing);
        Stereo result=input;
        if (captured && length>0) {
            Stereo loop=slice[read];
            // Preserve the exact slice period while easing the splice endpoint.
            const size_t edge=std::min(length/4,size_t(std::min(smoothing,int(sampleRate*0.001))));
            if (edge>1 && read>=length-edge) {
                const double t=double(read-(length-edge))/double(edge-1);
                for (int ch=0;ch<2;++ch) loop[ch]+=t*(slice[0][ch]-loop[ch]);
            }
            for (int ch=0;ch<2;++ch) result[ch]+=weight*(loop[ch]-input[ch]);
            if (++read==length) read=0;
            ++age;
        }
        if (!active && blend.value==0.) {captured=false; length=0;}
        history[write]=input; if (++write==capacity) write=0;
        available=std::min(available+1,capacity-2);
        requested=active; lastCell=cell;
        return result;
    }
private:
    std::vector<Stereo> history,slice;
    size_t capacity=0,write=0,available=0,read=0,length=0,age=0;
    double sampleRate=48000.,expectedPpq=0.;
    bool captured=false,pending=false,requested=false,wasPlaying=false,clockValid=false;
    int lastCell=-1;
    Ramp blend;
};
} // namespace Nabd
