#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include "../source/NabdGate.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace Nabd;
static int checks=0;
static void expect(bool condition,const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct Handler : IComponentHandler {
    std::vector<std::pair<ParamID,double>> edits;
    int begins=0,ends=0;
    tresult PLUGIN_API queryInterface(const TUID id,void** obj) override {
        if (FUnknownPrivate::iidEqual(id,IComponentHandler::iid) || FUnknownPrivate::iidEqual(id,FUnknown::iid)) {
            *obj=static_cast<IComponentHandler*>(this); return kResultOk;
        }
        *obj=nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 100; }
    uint32 PLUGIN_API release() override { return 100; }
    tresult PLUGIN_API beginEdit(ParamID) override { ++begins; return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id,ParamValue v) override { edits.emplace_back(id,v); return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { ++ends; return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};
void point(ParameterChanges& changes,ParamID id,double value,int offset=0) {
    int32 index=0; auto* q=changes.addParameterData(id,index); q->addPoint(offset,value,index);
}
struct Host {
    HMODULE library=nullptr;
    IPluginFactory* factory=nullptr;
    IComponent* component=nullptr;
    IAudioProcessor* processor=nullptr;
    IEditController* controller=nullptr;
    Handler handler;
    ProcessContext context{};
    Host(const char* path) {
        library=LoadLibraryA(path); expect(library!=nullptr,"LoadLibrary failed");
        auto init=reinterpret_cast<bool(*)()>(GetProcAddress(library,"InitDll"));
        expect(init && init(),"InitDll failed");
        auto get=reinterpret_cast<IPluginFactory*(PLUGIN_API*)()>(GetProcAddress(library,"GetPluginFactory"));
        expect(get!=nullptr,"Factory export absent"); factory=get();
        expect(factory->countClasses()==2,"Processor/controller factory registration");
        expect(factory->createInstance(ProcessorID,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"Create processor");
        expect(component->initialize(nullptr)==kResultOk,"Initialize processor");
        expect(component->queryInterface(IAudioProcessor::iid,reinterpret_cast<void**>(&processor))==kResultOk,"Audio interface");
        expect(factory->createInstance(ControllerID,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"Create controller");
        expect(controller->initialize(nullptr)==kResultOk,"Initialize controller");
        controller->setComponentHandler(&handler);
        expect(controller->getParameterCount()==138,"Parameter count including gate and repeat patterns");
        ProcessSetup setup{}; setup.sampleRate=48000.; setup.maxSamplesPerBlock=24000;
        setup.symbolicSampleSize=kSample32; setup.processMode=kRealtime;
        expect(processor->setupProcessing(setup)==kResultOk,"Setup");
        component->activateBus(kAudio,kInput,0,true); component->activateBus(kAudio,kOutput,0,true);
        expect(component->setActive(true)==kResultOk,"Activate");
        expect(processor->setProcessing(true)==kResultOk,"Start processing");
        context.sampleRate=48000.; context.tempo=120.;
        context.state=ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kPlaying;
    }
    ~Host() {
        if (processor) { processor->setProcessing(false); processor->release(); }
        if (component) component->setActive(false);
        if (controller) { controller->setComponentHandler(nullptr); controller->terminate(); controller->release(); }
        if (component) { component->terminate(); component->release(); }
        if (factory) factory->release();
        if (library) { auto exit=reinterpret_cast<bool(*)()>(GetProcAddress(library,"ExitDll")); if(exit) exit(); FreeLibrary(library); }
    }
    void flush(ParameterChanges& changes) {
        ProcessData data{}; data.symbolicSampleSize=kSample32; data.inputParameterChanges=&changes;
        expect(processor->process(data)==kResultOk,"Zero-sample parameter flush");
    }
    void set(ParamID id,double v) { ParameterChanges changes; point(changes,id,v); flush(changes); }
    void pattern(bool on=false) {
        ParameterChanges changes;
        for (int i=0;i<Steps;++i) point(changes,i,on?1.:0.);
        point(changes,Smooth,0.); point(changes,Wet,1.); point(changes,Bypass,0.); point(changes,Rate,0.4); point(changes,PatternCount,0.);
        point(changes,RepeatEnabled,0.);point(changes,GateEnabled,1.);
        flush(changes);
    }
    std::vector<float> block(double ppq,int n=512,ParameterChanges* changes=nullptr,bool inplace=false,bool silence=false,bool withContext=true,const std::vector<float>* signal=nullptr) {
        std::vector<float> left(n,1.f),right(n,0.25f),outLeft(n,-8.f),outRight(n,-8.f);
        if(signal){expect(int(signal->size())==n,"Input signal length");left=*signal;for(int i=0;i<n;++i)right[i]=left[i]*0.25f;}
        float* ins[]{left.data(),right.data()};
        float* outs[]{inplace?left.data():outLeft.data(),inplace?right.data():outRight.data()};
        AudioBusBuffers input{},output{};
        input.numChannels=2; input.channelBuffers32=ins; input.silenceFlags=silence?3:0;
        output.numChannels=2; output.channelBuffers32=outs;
        context.projectTimeMusic=ppq;
        ParameterChanges display;
        ProcessData data{}; data.numSamples=n; data.numInputs=1; data.numOutputs=1;
        data.inputs=&input; data.outputs=&output; data.symbolicSampleSize=kSample32;
        data.processContext=withContext?&context:nullptr; data.inputParameterChanges=changes; data.outputParameterChanges=&display;
        expect(processor->process(data)==kResultOk,"Process float stereo");
        for(int q=0;q<display.getParameterCount();++q) {
            auto* queue=display.getParameterData(q); int32 offset=0; double v=0;
            if(queue->getPoint(0,offset,v)==kResultOk) controller->setParamNormalized(queue->getParameterId(),v);
        }
        for(int i=0;i<n;++i) expect(std::abs(outs[1][i]-outs[0][i]*0.25f)<1.e-6f,"Stereo channels use identical gate");
        if (silence) {
            bool sound=false;for(int i=0;i<n;++i)if(outs[0][i]!=0.f || outs[1][i]!=0.f)sound=true;
            expect(output.silenceFlags==(sound?0:3),"Silence flags describe output, including repeated audio");
        }
        return inplace?left:outLeft;
    }
};
void savePreview(HWND child,const char* path,int width,int height) {
    auto dc=GetDC(nullptr); auto memory=CreateCompatibleDC(dc);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr; auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    auto old=SelectObject(memory,bitmap); SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
    BITMAPFILEHEADER file{}; file.bfType=0x4D42; file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader);
    file.bfSize=file.bfOffBits+width*height*4;
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<char*>(&file),sizeof(file));
    out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    out.write(static_cast<char*>(pixels),width*height*4);
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,dc);
}
int main(int argc,char** argv) {
    try {
        expect(argc>=3,"Usage: HostTests plugin.vst3 preview.bmp");
        Host host(argv[1]);
        host.pattern(); host.set(0,1.);
        auto samples=host.block(0.,18000);
        expect(samples[0]==1 && samples[5999]==1 && samples[6000]==0 && samples[11999]==0,"120 BPM sixteenth boundaries");
        host.context.tempo=150.; samples=host.block(0.,12000);
        expect(samples[4799]==1 && samples[4800]==0,"Tempo change to 150 BPM");
        host.context.tempo=120.; samples=host.block(-0.25,12000);
        expect(samples[0]==0 && samples[5999]==0 && samples[6000]==1,"Negative preroll wraps into first step");
        samples=host.block(0.,64); expect(samples[0]==1,"Loop/seek immediately realigns");
        samples=host.block(0.5,64); expect(samples[0]==0,"Seek to muted step");
        for (int rate=0;rate<6;++rate) {
            host.set(Rate,double(rate)/5.);
            const double stepLength=1./StepRates[rate];
            samples=host.block(stepLength,64); expect(samples[0]==0,"All six step divisions advance");
            samples=host.block(stepLength*16.,64); expect(samples[0]==1,"All six divisions wrap");
        }
        host.set(Rate,0.4);
        ParameterChanges multi;
        for(int row=0;row<MaxPatterns;++row) for(int i=0;i<Steps;++i) point(multi,stepParam(row,i),row%2==0?1.:0.);
        point(multi,PatternCount,1./3.);host.flush(multi);
        samples=host.block(3.99,512);
        expect(samples[239]==1 && samples[240]==0,"Two patterns advance exactly at the row boundary");
        samples=host.block(4.,64);expect(samples[0]==0,"Second pattern has its own mute data");
        expect(std::lround(host.controller->getParamNormalized(Playhead)*64.)==17,"Playhead identifies second pattern");
        samples=host.block(8.,64);expect(samples[0]==1,"Two-pattern sequence loops after eight beats");
        host.set(PatternCount,1.);
        samples=host.block(8.,64);expect(samples[0]==1,"Four-pattern sequence reaches third row");
        samples=host.block(12.,64);expect(samples[0]==0,"Four-pattern sequence reaches fourth row");
        samples=host.block(16.,64);expect(samples[0]==1,"Four-pattern sequence wraps after sixteen beats");
        samples=host.block(-0.25,64);expect(samples[0]==0,"Negative preroll wraps through all active patterns");
        host.set(PatternCount,0.);
        ParameterChanges rowAutomation;point(rowAutomation,PatternCount,1./3.,32);
        samples=host.block(4.,128,&rowAutomation);
        expect(samples[31]==1 && samples[32]==0,"Pattern count automation applies at sample offset");
        host.pattern();host.set(0,1.);
        ParameterChanges automation; point(automation,1,1.,32);
        samples=host.block(0.25,128,&automation);
        expect(samples[31]==0 && samples[32]==1,"Sample-offset step automation");
        host.pattern(); host.set(Smooth,0.15); host.processor->setProcessing(true);
        samples=host.block(0.,512);
        expect(samples[0]<1 && samples[0]>0.99 && samples[143]==0 && samples[511]==0,"3ms smoothing reaches exact mute");
        for(size_t i=1;i<samples.size();++i) expect(std::abs(samples[i]-samples[i-1])<0.0071f,"Smoothed edge has bounded slope");
        host.set(Bypass,1.); samples=host.block(0.,512); expect(samples.back()==1,"Bypass passes original signal");
        host.set(Bypass,0.); host.set(Wet,0.); samples=host.block(0.,512); expect(samples.back()==1,"Dry mix passes original signal");
        host.pattern(); host.context.state &= ~ProcessContext::kPlaying;
        samples=host.block(0.,512); expect(samples.back()==1,"Stopped transport passes preview sound");
        host.context.state |= ProcessContext::kPlaying;
        samples=host.block(0.,512,nullptr,false,false,false); expect(samples.back()==1,"Missing host context safely passes dry");
        host.pattern(true); samples=host.block(0.,512,nullptr,true); expect(samples.back()==1,"In-place processing");
        samples=host.block(0.,512,nullptr,false,true); expect(samples.back()==0,"Silent buffers ignore stale sample data");
        // Test the real DLL with a changing waveform: constant input would hide
        // a fake repeat implementation that only forwards or gates the input.
        auto wave=[](int n,int offset=0){std::vector<float> v(n);for(int i=0;i<n;++i)v[i]=float(0.3*std::sin((i+offset)*0.013)+0.15*std::sin((i+offset)*0.0037));return v;};
        auto repeatSetup=[&](int activeCell,int size=3){
            host.pattern(true);ParameterChanges changes;
            for(int row=0;row<MaxPatterns;++row)for(int i=0;i<Steps;++i)point(changes,repeatParam(row,i),i==activeCell?1.:0.);
            point(changes,RepeatEnabled,1.);point(changes,GateEnabled,0.);point(changes,RepeatSize,double(size)/5.);
            host.flush(changes);host.context.tempo=120.;host.processor->setProcessing(true);
        };
        repeatSetup(1);auto changing=wave(18000);
        samples=host.block(0.,18000,nullptr,false,false,true,&changing);
        for(int i=0;i<18000;++i){const int source=i>=6000 && i<12000?3000+(i-6000)%3000:i;expect(std::abs(samples[i]-changing[source])<1.e-6f,"Repeat captures and loops the preceding slice exactly");}
        repeatSetup(0);host.set(repeatParam(0,1),1.);host.set(repeatParam(0,2),1.);
        samples=host.block(0.,18000,nullptr,true,false,true,&changing);
        expect(samples[10]==changing[10] && samples[3000]==changing[0],"Startup stays live until a complete slice is available");
        expect(samples[6000]==changing[3000] && samples[12000]==changing[9000],"Adjacent active cells retrigger fresh live input");
        // Every advertised musical slice length, using a second tempo.
        for(int size=0;size<6;++size){
            repeatSetup(1,size);host.set(Rate,0.);host.context.tempo=150.;
            auto warm=wave(19200),live=wave(5000,19200);
            host.block(0.,19200,nullptr,false,false,true,&warm);
            samples=host.block(1.,5000,nullptr,false,false,true,&live);
            const int length=int(std::lround(19200.*RepeatBeats[size]));
            for(int i=0;i<5000;++i)expect(std::abs(samples[i]-warm[19200-length+i%length])<1.e-6f,"All repeat sizes follow host BPM");
        }
        repeatSetup(0);host.set(Rate,0.);auto warm=wave(6000),live=wave(512,6000);
        host.block(0.,6000,nullptr,false,false,true,&warm);host.context.tempo=150.;
        samples=host.block(0.25,512,nullptr,false,false,true,&live);
        expect(std::abs(samples[0]-warm[3600])<1.e-6f,"Tempo change recaptures the correctly sized slice");
        host.set(Bypass,1.);auto next=wave(512,6512);
        samples=host.block(0.25+512./19200.,512,nullptr,false,false,true,&next);
        expect(samples.back()==next.back(),"Bypass returns live input during repeat");
        host.set(Bypass,0.);host.set(Wet,0.);auto dry=wave(512,7024);
        samples=host.block(0.25+1024./19200.,512,nullptr,false,false,true,&dry);
        expect(samples.back()==dry.back(),"Wet zero returns live input during repeat");
        repeatSetup(1);host.set(RepeatEnabled,0.);warm=wave(6000);live=wave(128,6000);
        host.block(0.,6000,nullptr,false,false,true,&warm);
        ParameterChanges repeatAutomation;point(repeatAutomation,RepeatEnabled,1.,32);
        samples=host.block(0.25,128,&repeatAutomation,false,false,true,&live);
        expect(samples[31]==live[31] && std::abs(samples[32]-warm[3032])<1.e-6f,"Repeat enable automation observes sample offsets");
        host.set(GateEnabled,1.);host.set(1,0.);live=wave(128,6128);
        samples=host.block(0.25+128./24000.,128,nullptr,false,false,true,&live);
        expect(samples.back()==0.,"Gate can mute repeated audio when both sections run");
        repeatSetup(0);warm=wave(4000);host.block(0.,4000,nullptr,false,false,true,&warm);
        samples=host.block(4000./24000.,512,nullptr,false,true);
        expect(std::any_of(samples.begin(),samples.end(),[](float x){return x!=0.f;}),"Buffered repeat remains audible over silent live input");
        std::vector<float> zeros(512,0.f);
        samples=host.block(5.,512,nullptr,false,false,true,&zeros);
        expect(std::all_of(samples.begin(),samples.end(),[](float x){return x==0.f;}),"Seeking discards stale recorded audio");
        host.context.state &= ~ProcessContext::kPlaying;
        samples=host.block(0.,512,nullptr,false,false,true,&zeros);expect(samples.back()==0.,"Stopped repeat passes live input");
        host.context.state |= ProcessContext::kPlaying;
        samples=host.block(0.,512,nullptr,false,false,true,&zeros);expect(samples.back()==0.,"Restart cannot replay an old capture");
        host.pattern(); host.set(3,1.); host.set(Rate,0.8); host.set(Smooth,0.37); host.set(Wet,0.62); host.set(Bypass,1.);
        host.set(stepParam(1,6),1.);host.set(stepParam(3,13),1.);host.set(PatternCount,1.);
        host.set(repeatParam(3,14),1.);host.set(RepeatEnabled,1.);host.set(RepeatSize,0.8);host.set(GateEnabled,0.);
        MemoryStream state; expect(host.component->getState(&state)==kResultOk,"Save plugin state");
        host.pattern(true); state.seek(0,IBStream::kIBSeekSet,nullptr);
        expect(host.component->setState(&state)==kResultOk,"Restore processor state");
        state.seek(0,IBStream::kIBSeekSet,nullptr);
        expect(host.controller->setComponentState(&state)==kResultOk,"Restore controller state");
        expect(host.controller->getParamNormalized(3)==1 && host.controller->getParamNormalized(2)==0,"Pattern saved and restored");
        expect(std::abs(host.controller->getParamNormalized(Smooth)-0.37)<1.e-9 && host.controller->getParamNormalized(Bypass)==1,"Controls saved and restored");
        expect(host.controller->getParamNormalized(stepParam(1,6))==1 && host.controller->getParamNormalized(stepParam(3,13))==1 && host.controller->getParamNormalized(PatternCount)==1,"All pattern rows and sequence length restore");
        expect(host.controller->getParamNormalized(repeatParam(3,14))==1 && host.controller->getParamNormalized(RepeatEnabled)==1 && host.controller->getParamNormalized(RepeatSize)==0.8 && host.controller->getParamNormalized(GateEnabled)==0,"Repeat pattern, size and section switches restore");
        MemoryStream corrupt; int32 bad=123; corrupt.write(&bad,sizeof(bad),nullptr);
        corrupt.seek(0,IBStream::kIBSeekSet,nullptr); expect(host.component->setState(&corrupt)!=kResultOk,"Reject corrupt state");
        MemoryStream legacy;IBStreamer oldWriter(&legacy,kLittleEndian);
        oldWriter.writeInt32(0x4E424447);oldWriter.writeInt32(1);
        for(auto v:LegacyDefaults)oldWriter.writeDouble(v);
        legacy.seek(0,IBStream::kIBSeekSet,nullptr);expect(host.component->setState(&legacy)==kResultOk,"Load original version 1.0 project state");
        legacy.seek(0,IBStream::kIBSeekSet,nullptr);expect(host.controller->setComponentState(&legacy)==kResultOk,"Migrate original controller state");
        expect(host.controller->getParamNormalized(PatternCount)==0,"Old projects retain a single pattern");
        expect(host.controller->getParamNormalized(RepeatEnabled)==0 && host.controller->getParamNormalized(GateEnabled)==1,"Version 1.0 keeps repeat disabled");
        for(int i=0;i<Steps;++i)expect(host.controller->getParamNormalized(i)==LegacyDefaults[i] && host.controller->getParamNormalized(stepParam(1,i))==LegacyDefaults[i],"Legacy steps preserved with useful additional-row defaults");
        MemoryStream version11;IBStreamer v11(&version11,kLittleEndian);v11.writeInt32(0x4E424447);v11.writeInt32(2);
        auto oldValues=Defaults;oldValues[PatternCount]=1.;oldValues[stepParam(3,10)]=1.;
        for(int i=0;i<Version11Count;++i)v11.writeDouble(oldValues[i]);
        version11.seek(0,IBStream::kIBSeekSet,nullptr);expect(host.component->setState(&version11)==kResultOk,"Load version 1.1 state");
        version11.seek(0,IBStream::kIBSeekSet,nullptr);expect(host.controller->setComponentState(&version11)==kResultOk,"Load version 1.1 controller state");
        expect(host.controller->getParamNormalized(PatternCount)==1 && host.controller->getParamNormalized(stepParam(3,10))==1 && host.controller->getParamNormalized(RepeatEnabled)==0,"Version 1.1 retains gate rows without activating repeat");
        SpeakerArrangement mono=SpeakerArr::kMono;
        host.processor->setProcessing(false); host.component->setActive(false);
        expect(host.processor->setBusArrangements(&mono,1,&mono,1)==kResultOk,"Mono layout");
        ProcessSetup setup{};setup.sampleRate=96000.;setup.maxSamplesPerBlock=512;setup.symbolicSampleSize=kSample64;
        expect(host.processor->setupProcessing(setup)==kResultOk,"96kHz double processing setup");
        host.pattern(true); host.component->setActive(true);host.processor->setProcessing(true);
        double in[512],out[512];std::fill_n(in,512,0.625);double* input[]{in};double* output[]{out};
        AudioBusBuffers ib{},ob{};ib.numChannels=ob.numChannels=1;ib.channelBuffers64=input;ob.channelBuffers64=output;
        ProcessData data{};data.symbolicSampleSize=kSample64;data.numSamples=512;data.numInputs=data.numOutputs=1;
        data.inputs=&ib;data.outputs=&ob;data.processContext=&host.context;
        expect(host.processor->process(data)==kResultOk && out[511]==0.625,"64-bit mono at 96kHz");
        host.set(GateEnabled,0.);host.set(RepeatEnabled,1.);host.set(RepeatSize,0.8);host.set(Rate,0.);
        host.set(repeatParam(0,0),1.);host.context.tempo=120.;host.context.sampleRate=96000.;host.processor->setProcessing(true);
        for(int block=0;block<14;++block){
            for(int i=0;i<512;++i)in[i]=double(block*512+i)/10000.;
            host.context.projectTimeMusic=double(block*512)/48000.;
            expect(host.processor->process(data)==kResultOk,"64-bit mono repeat processing");
            for(int i=0;i<512;++i){const int n=block*512+i;const int source=n<3000?n:(n-3000)%3000;expect(std::abs(out[i]-double(source)/10000.)<1.e-12,"Double-precision repeat preserves the exact captured samples");}
        }
        // Restore controller defaults for UI interaction and actual native render.
        for(uint32 id=0;id<EditableCount;++id)if(isEditable(id))host.controller->setParamNormalized(id,Defaults[id]);
        host.controller->setParamNormalized(Tempo,0.128);host.controller->setParamNormalized(Playhead,21./64.);
        auto* view=host.controller->createView(ViewType::kEditor);expect(view!=nullptr,"Create native editor");
        auto parent=CreateWindowExW(0,L"STATIC",L"Nabd test host",WS_OVERLAPPED,0,0,920,594,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        expect(parent!=nullptr,"Create hidden test host window");
        expect(view->attached(parent,kPlatformTypeHWND)==kResultOk,"Attach native editor");
        auto child=GetWindow(parent,GW_CHILD);expect(child!=nullptr,"Editor child exists");
        auto click=[&](int x,int y) {SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(x,y));};
        click(55,250);expect(host.controller->getParamNormalized(0)==0,"Click toggles step off");
        click(55,250);expect(host.controller->getParamNormalized(0)==1,"Click toggles step on");
        click(720,404);expect(host.controller->getParamNormalized(0)==0 && host.controller->getParamNormalized(15)==0,"All Off action");
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(55,250));
        SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(110,250));
        SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(110,250));
        expect(host.controller->getParamNormalized(0)==1 && host.controller->getParamNormalized(1)==1,"Drag paints matching values");
        click(160,330);expect(host.controller->getParamNormalized(stepParam(1,2))==0,"Second-row click edits an independent pattern");
        click(630,404);expect(host.controller->getParamNormalized(stepParam(1,2))==1 && host.controller->getParamNormalized(2)==0,"Preset affects selected row only");
        click(400,186);expect(patternCount(host.controller->getParamNormalized(PatternCount))==3,"Add Pattern creates third row");
        ViewRect added{};view->getSize(&added);expect(added.getHeight()==668,"Editor grows when a pattern is added");
        click(400,186);expect(patternCount(host.controller->getParamNormalized(PatternCount))==4,"Add Pattern creates fourth row");
        click(400,186);expect(patternCount(host.controller->getParamNormalized(PatternCount))==4,"Add Pattern stops at supported maximum");
        click(213,186);expect(patternCount(host.controller->getParamNormalized(PatternCount))==2,"Count buttons reduce active sequence length");
        click(180,139);expect(host.controller->getParamNormalized(Rate)==0,"Rate button automation");
        click(850,57);expect(host.controller->getParamNormalized(Bypass)==1,"Bypass button");
        click(240,502);expect(host.controller->getParamNormalized(Smooth)>0.4,"Smoothing slider");
        expect(host.handler.begins==host.handler.ends,"Balanced automation gestures");
        for(uint32 id=0;id<EditableCount;++id)if(isEditable(id))host.controller->setParamNormalized(id,Defaults[id]);
        click(230,89);ViewRect repeatSize{};view->getSize(&repeatSize);
        expect(repeatSize.getHeight()==642,"Repeat tab exposes musical size controls");
        click(850,89);expect(host.controller->getParamNormalized(RepeatEnabled)==1,"Repeat section toggle");
        click(80,89);click(850,89);expect(host.controller->getParamNormalized(GateEnabled)==0,"Gate can be disabled independently");
        click(230,89);click(510,186);expect(host.controller->getParamNormalized(RepeatSize)==0.8,"Repeat size button");
        click(55,304);expect(host.controller->getParamNormalized(repeatParam(0,0))==1 && host.controller->getParamNormalized(0)==1,"Repeat grid is independent from gate grid");
        click(720,452);expect(host.controller->getParamNormalized(repeatParam(0,0))==0 && host.controller->getParamNormalized(0)==1,"Repeat All Off leaves gate cells intact");
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(55,304));SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(110,304));SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(110,304));
        expect(host.controller->getParamNormalized(repeatParam(0,0))==1 && host.controller->getParamNormalized(repeatParam(0,1))==1,"Repeat drag painting");
        const auto repeatPreview=std::string(argv[2])+".repeat.bmp";savePreview(child,repeatPreview.c_str(),920,642);
        click(80,89);for(uint32 id=0;id<EditableCount;++id)if(isEditable(id))host.controller->setParamNormalized(id,Defaults[id]);
        savePreview(child,argv[2],920,594);
        IPlugViewContentScaleSupport* scaling=nullptr;
        expect(view->queryInterface(IPlugViewContentScaleSupport::iid,reinterpret_cast<void**>(&scaling))==kResultOk,"DPI interface");
        expect(scaling->setContentScaleFactor(1.5f)==kResultOk,"150 percent DPI");
        ViewRect size{};view->getSize(&size);expect(size.getWidth()==1380 && size.getHeight()==891,"DPI geometry with multiple rows");
        scaling->release();expect(view->removed()==kResultOk,"Remove editor");
        expect(view->attached(parent,kPlatformTypeHWND)==kResultOk,"Reopen editor");
        view->removed();view->release();DestroyWindow(parent);
        std::printf("PASS: %d checks (DLL load, sync, audio, automation, state, native editor, DPI).\n",checks);
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
