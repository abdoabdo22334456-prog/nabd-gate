#pragma once
#include "GateEngine.h"
#include "StutterEngine.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Nabd {
using namespace Steinberg;
using namespace Steinberg::Vst;
inline const FUID ProcessorID(0x7AD19FA2,0x5C284B31,0xA982365F,0xA2ED7810);
inline const FUID ControllerID(0x12DC35BE,0x9A764E4C,0xBA49503D,0x851CC678);

class Processor final : public AudioEffect {
public:
    Processor();
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new Processor); }
    tresult PLUGIN_API initialize(FUnknown*) override;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*,int32,SpeakerArrangement*,int32) override;
    tresult PLUGIN_API canProcessSampleSize(int32) override;
    tresult PLUGIN_API setupProcessing(ProcessSetup&) override;
    tresult PLUGIN_API setActive(TBool) override;
    tresult PLUGIN_API setProcessing(TBool) override;
    tresult PLUGIN_API process(ProcessData&) override;
    uint32 PLUGIN_API getTailSamples() override { return uint32(engine.sampleRate*16.); }
    tresult PLUGIN_API setState(IBStream*) override;
    tresult PLUGIN_API getState(IBStream*) override;
private:
    GateEngine engine;
    StutterEngine stutter;
    template<class Sample> void render(ProcessData&, double, double, bool,double);
};
class Controller final : public EditController {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new Controller); }
    tresult PLUGIN_API initialize(FUnknown*) override;
    tresult PLUGIN_API setComponentState(IBStream*) override;
    IPlugView* PLUGIN_API createView(FIDString) override;
    void edit(ParamID id, double value) {
        beginEdit(id); setParamNormalized(id, value); performEdit(id, value); endEdit(id);
    }
};
IPlugView* createEditor(Controller*);
void releaseEditorClass();
} // namespace Nabd
