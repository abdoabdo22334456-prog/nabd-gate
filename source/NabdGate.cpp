#include "NabdGate.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/main/pluginfactory.h"
#include <array>
#include <cstring>

namespace Nabd {
namespace {
bool readState(IBStream* stream, std::array<double, EditableCount>& values) {
    if (!stream) return false;
    IBStreamer io(stream,kLittleEndian);
    int32 magic=0, version=0;
    if (!io.readInt32(magic) || magic!=0x4E424447 || !io.readInt32(version) || version<1 || version>3) return false;
    auto result=Defaults;
    const int count=version==1 ? LegacyEditableCount : (version==2 ? Version11Count : EditableCount);
    for (int i=0;i<count;++i) {
        auto& v=result[i];
        if (!io.readDouble(v) || !std::isfinite(v)) return false;
        v=normalized(v);
    }
    if (version==1) {
        // Existing projects retain their original one-pattern loop exactly.
        result[PatternCount]=0.;
        for (int row=1;row<MaxPatterns;++row)
            for (int step=0;step<Steps;++step) result[stepParam(row,step)]=result[step];
    }
    values=result;
    return true;
}
}
Processor::Processor() {
    setControllerClass(ControllerID);
    processContextRequirements.needTempo().needProjectTimeMusic().needTransportState();
}
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const auto result=AudioEffect::initialize(context);
    if (result!=kResultOk) return result;
    addAudioInput(STR16("Input"),SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"),SpeakerArr::kStereo);
    return kResultOk;
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* ins,int32 ni,SpeakerArrangement* outs,int32 no) {
    if (ni!=1 || no!=1 || !ins || !outs || ins[0]!=outs[0] ||
        (ins[0]!=SpeakerArr::kMono && ins[0]!=SpeakerArr::kStereo)) return kResultFalse;
    return AudioEffect::setBusArrangements(ins,ni,outs,no);
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size==kSample32 || size==kSample64 ? kResultTrue : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    if (!std::isfinite(setup.sampleRate) || setup.sampleRate<1. || setup.sampleRate>768000.) return kResultFalse;
    try { stutter.prepare(setup.sampleRate); } catch (...) { return kResultFalse; }
    engine.prepare(setup.sampleRate);
    return AudioEffect::setupProcessing(setup);
}
tresult PLUGIN_API Processor::setActive(TBool active) {
    if (active) {engine.prepare(processSetup.sampleRate); stutter.reset();}
    return AudioEffect::setActive(active);
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    if (state) {engine.prepare(processSetup.sampleRate); stutter.reset();}
    return kResultOk;
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    if (!stream) return kInvalidArgument;
    IBStreamer io(stream,kLittleEndian);
    if (!io.writeInt32(0x4E424447) || !io.writeInt32(3)) return kResultFalse;
    for (auto v:engine.params) if (!io.writeDouble(v)) return kResultFalse;
    return kResultOk;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    auto values=Defaults;
    if (!readState(stream,values)) return kResultFalse;
    engine.params=values;
    stutter.reset();
    return kResultOk;
}
template<class Sample>
void Processor::render(ProcessData& data, double ppq, double delta, bool playing,double tempo) {
    // One queue cursor per editable parameter. No locks or allocation in the audio callback.
    struct Cursor { IParamValueQueue* queue=nullptr; int32 point=0, offset=0; double value=0.; bool valid=false; };
    std::array<Cursor, EditableCount> changes {};
    if (data.inputParameterChanges) {
        for (int32 i=0;i<data.inputParameterChanges->getParameterCount();++i) {
            auto* queue=data.inputParameterChanges->getParameterData(i);
            if (!queue || !isEditable(queue->getParameterId())) continue;
            auto& c=changes[queue->getParameterId()]; c.queue=queue;
            c.valid=queue->getPointCount()>0 && queue->getPoint(0,c.offset,c.value)==kResultTrue;
        }
    }
    auto apply=[&](int32 sample) {
        for (uint32_t id=0;id<EditableCount;++id) {
            auto& c=changes[id];
            while (c.valid && c.offset<=sample) {
                engine.set(id,c.value);
                ++c.point;
                c.valid=c.point<c.queue->getPointCount() && c.queue->getPoint(c.point,c.offset,c.value)==kResultTrue;
            }
        }
    };
    apply(0); // VST3 also sends parameter-only flush calls with zero audio samples.
    stutter.beginBlock(ppq,delta,playing,data.numSamples);
    if (data.numOutputs<1 || !data.outputs || data.outputs[0].numChannels<1) return;
    auto& out=data.outputs[0];
    AudioBusBuffers* in=data.numInputs>0 && data.inputs ? &data.inputs[0] : nullptr;
    Sample** output;
    Sample** input;
    if constexpr (sizeof(Sample)==sizeof(float)) {
        output=out.channelBuffers32; input=in ? in->channelBuffers32 : nullptr;
    } else {
        output=out.channelBuffers64; input=in ? in->channelBuffers64 : nullptr;
    }
    if (!output) return;
    uint64 silent=0;
    for (int32 ch=0;ch<out.numChannels && ch<64;++ch) silent |= uint64(1)<<ch;
    for (int32 i=0;i<data.numSamples;++i) {
        if (i>0) apply(i);
        const auto frame=engine.advance(ppq+double(i)*delta,playing);
        StutterEngine::Stereo dry {};
        for (int32 ch=0;ch<std::min(2,out.numChannels);++ch) {
            const bool hasInput=in && input && ch<in->numChannels && input[ch] && !(in->silenceFlags & (uint64(1)<<ch));
            dry[ch]=hasInput ? double(input[ch][i]) : 0.;
        }
        const bool repeat=engine.params[RepeatEnabled]>=0.5 && engine.params[repeatParam(frame.position/Steps,frame.position%Steps)]>=0.5;
        const auto repeated=stutter.process(dry,frame.position,repeat,playing,tempo,rateIndex(engine.params[RepeatSize]),engine.params[Smooth]);
        for (int32 ch=0;ch<out.numChannels;++ch) {
            if (!output[ch]) continue;
            const Sample sample=ch<2 ? Sample(dry[ch]+frame.amount*(repeated[ch]*frame.gate-dry[ch])) : Sample(0);
            output[ch][i]=sample;
            if (sample!=Sample(0) && ch<64) silent &= ~(uint64(1)<<ch);
        }
    }
    out.silenceFlags=silent;
}
tresult PLUGIN_API Processor::process(ProcessData& data) {
    double tempo=120., ppq=0.; bool playing=false;
    const auto* context=data.processContext;
    if (context) {
        if ((context->state & ProcessContext::kTempoValid) && std::isfinite(context->tempo) && context->tempo>0.) tempo=context->tempo;
        playing=(context->state & ProcessContext::kPlaying)!=0;
        if ((context->state & ProcessContext::kProjectTimeMusicValid) && std::isfinite(context->projectTimeMusic))
            ppq=context->projectTimeMusic;
        else ppq=double(context->projectTimeSamples)/engine.sampleRate*tempo/60.;
    }
    const double delta=tempo/(60.*engine.sampleRate);
    if (data.symbolicSampleSize==kSample64) render<double>(data,ppq,delta,playing,tempo);
    else if (data.symbolicSampleSize==kSample32) render<float>(data,ppq,delta,playing,tempo);
    else return kResultFalse;
    if (data.outputParameterChanges) {
        auto send=[&](ParamID id,double value) {
            int32 index=0,point=0;
            if (auto* queue=data.outputParameterChanges->addParameterData(id,index))
                queue->addPoint(0,value,point);
        };
        const int step=sequenceAt(ppq+std::max(0,data.numSamples-1)*delta,StepRates[rateIndex(engine.params[Rate])],patternCount(engine.params[PatternCount]));
        send(Playhead,playing ? double(step+1)/(Steps*MaxPatterns) : 0.);
        send(Tempo,context ? std::clamp(tempo/1000.,0.,1.) : 0.);
    }
    return kResultOk;
}
tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    auto result=EditController::initialize(context);
    if (result!=kResultOk) return result;
    auto addStep=[&](int row,int i) {
        const auto id=stepParam(row,i);
        String128 name {}; auto label=row==0 ? std::string("Step ")+std::to_string(i+1) :
            std::string("Pattern ")+std::to_string(row+1)+" Step "+std::to_string(i+1);
        for (size_t c=0;c<label.size();++c) name[c]=TChar(label[c]);
        auto* p=new StringListParameter(name,id,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);
        p->appendString(STR16("Mute")); p->appendString(STR16("Pass"));
        p->getInfo().defaultNormalizedValue=Defaults[id]; p->setNormalized(Defaults[id]); parameters.addParameter(p);
    };
    for (int i=0;i<Steps;++i) addStep(0,i);
    auto* rate=new StringListParameter(STR16("Step rate"),Rate,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);
    rate->appendString(STR16("1/4")); rate->appendString(STR16("1/8"));
    rate->appendString(STR16("1/16")); rate->appendString(STR16("1/32"));
    rate->appendString(STR16("1/8 triplet")); rate->appendString(STR16("1/16 triplet"));
    rate->getInfo().defaultNormalizedValue=Defaults[Rate]; rate->setNormalized(Defaults[Rate]); parameters.addParameter(rate);
    parameters.addParameter(new RangeParameter(STR16("Smoothing"),Smooth,STR16("ms"),0.,20.,3.));
    parameters.addParameter(new RangeParameter(STR16("Wet"),Wet,STR16("%"),0.,100.,100.));
    parameters.addParameter(STR16("Bypass"),nullptr,1,0.,ParameterInfo::kCanAutomate|ParameterInfo::kIsBypass,Bypass);
    for (int row=1;row<MaxPatterns;++row) for (int i=0;i<Steps;++i) addStep(row,i);
    auto* count=new StringListParameter(STR16("Pattern count"),PatternCount,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);
    count->appendString(STR16("1 pattern")); count->appendString(STR16("2 patterns"));
    count->appendString(STR16("3 patterns")); count->appendString(STR16("4 patterns"));
    count->getInfo().defaultNormalizedValue=Defaults[PatternCount]; count->setNormalized(Defaults[PatternCount]); parameters.addParameter(count);
    for (int row=0;row<MaxPatterns;++row) for (int i=0;i<Steps;++i) {
        const auto id=repeatParam(row,i);String128 name {};
        const auto label=std::string("Repeat P")+std::to_string(row+1)+" Step "+std::to_string(i+1);
        for(size_t c=0;c<label.size();++c)name[c]=TChar(label[c]);
        auto* p=new StringListParameter(name,id,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);
        p->appendString(STR16("Live"));p->appendString(STR16("Repeat"));
        p->getInfo().defaultNormalizedValue=Defaults[id];p->setNormalized(Defaults[id]);parameters.addParameter(p);
    }
    parameters.addParameter(STR16("Repeat enabled"),nullptr,1,0.,ParameterInfo::kCanAutomate,RepeatEnabled);
    auto* size=new StringListParameter(STR16("Repeat size"),RepeatSize,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);
    size->appendString(STR16("1/4"));size->appendString(STR16("1/8"));size->appendString(STR16("1/16"));
    size->appendString(STR16("1/32"));size->appendString(STR16("1/64"));size->appendString(STR16("1/16 triplet"));
    size->getInfo().defaultNormalizedValue=Defaults[RepeatSize];size->setNormalized(Defaults[RepeatSize]);parameters.addParameter(size);
    parameters.addParameter(STR16("Gate enabled"),nullptr,1,1.,ParameterInfo::kCanAutomate,GateEnabled);
    parameters.addParameter(STR16("Playhead"),nullptr,0,0.,ParameterInfo::kIsReadOnly|ParameterInfo::kIsHidden,Playhead);
    parameters.addParameter(STR16("Host tempo"),nullptr,0,0.,ParameterInfo::kIsReadOnly|ParameterInfo::kIsHidden,Tempo);
    return kResultOk;
}
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    auto values=Defaults;
    if (!readState(stream,values)) return kResultFalse;
    for (uint32_t id=0;id<EditableCount;++id) if(isEditable(id)) setParamNormalized(id,values[id]);
    return kResultOk;
}
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    return name && std::strcmp(name,ViewType::kEditor)==0 ? createEditor(this) : nullptr;
}
} // namespace Nabd

bool InitModule() { return true; }
bool DeinitModule() { Nabd::releaseEditorClass(); return true; }
using namespace Steinberg;
using namespace Steinberg::Vst;
BEGIN_FACTORY_DEF("Nabd Audio", "", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(Nabd::ProcessorID), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "Nabd Gate", Vst::kDistributable,
           "Fx|Modulation", "1.2.0", kVstVersionString, Nabd::Processor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(Nabd::ControllerID), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "Nabd Gate Controller", 0,
           "", "1.2.0", kVstVersionString, Nabd::Controller::create)
END_FACTORY
