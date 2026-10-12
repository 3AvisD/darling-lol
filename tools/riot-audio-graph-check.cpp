#include <AudioToolbox/AUGraph.h>
#include <AudioToolbox/AudioUnitParameters.h>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <initializer_list>
#include <atomic>

static std::atomic<unsigned> calls{0};
static std::atomic<bool> stopFromCallback{false};
static std::atomic<int> callbackStopResult{1};
static AUGraph callbackGraph=nullptr;
static OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                      UInt32, UInt32 frames, AudioBufferList* data) {
    ++calls;
    if (stopFromCallback.exchange(false)) {
        callbackStopResult.store(AUGraphStop(callbackGraph));
    }
    if (data->mNumberBuffers != 2) return -1;
    for (unsigned b=0; b<2; ++b)
        for (unsigned f=0; f<frames; ++f) ((float*)data->mBuffers[b].mData)[f] = b ? .2f : .1f;
    return 0;
}
#define CHECK(call) do { OSStatus s=(call); if(s) { printf("FAIL line=%d status=%d\n",__LINE__,s); return 1; } } while(0)
int main() {
    setbuf(stdout,nullptr);
    printf("AUDIO_GRAPH_CHECK started\n");
    AUGraph graph=nullptr;
    CHECK(NewAUGraph(&graph));
    AudioComponentDescription mix={kAudioUnitType_Mixer,kAudioUnitSubType_MatrixMixer,kAudioUnitManufacturer_Apple,0,0};
    AudioComponentDescription out={kAudioUnitType_Output,kAudioUnitSubType_DefaultOutput,kAudioUnitManufacturer_Apple,0,0};
    AUNode m,o; CHECK(AUGraphAddNode(graph,&mix,&m)); CHECK(AUGraphAddNode(graph,&out,&o));
    CHECK(AUGraphOpen(graph));
    AudioUnit mixer,output; CHECK(AUGraphNodeInfo(graph,m,nullptr,&mixer)); CHECK(AUGraphNodeInfo(graph,o,nullptr,&output));
    AudioStreamBasicDescription format={44100,kAudioFormatLinearPCM,
        kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked|kAudioFormatFlagIsNonInterleaved,4,1,4,2,32,0};
    CHECK(AudioUnitSetProperty(mixer,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format)));
    CHECK(AudioUnitSetProperty(mixer,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&format,sizeof(format)));
    CHECK(AudioUnitSetProperty(output,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format)));
    UInt32 buses=1; CHECK(AudioUnitSetProperty(mixer,kAudioUnitProperty_ElementCount,kAudioUnitScope_Input,0,&buses,sizeof(buses)));
    UInt32 frames=4096; CHECK(AudioUnitSetProperty(mixer,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&frames,sizeof(frames)));
    AURenderCallbackStruct cb={input,nullptr}; CHECK(AUGraphSetNodeInputCallback(graph,m,0,&cb));
    CHECK(AUGraphConnectNodeInput(graph,m,0,o,0)); CHECK(AUGraphInitialize(graph));
    // Exercise the exact packed global elements used by the game.
    for (UInt32 e : {0xffffffffu,0xffff0000u,0xffff0001u,0x0000ffffu,0x0001ffffu})
        CHECK(AudioUnitSetParameter(mixer,kMatrixMixerParam_Volume,kAudioUnitScope_Global,e,1,0));
    CHECK(AudioUnitSetParameter(mixer,kMatrixMixerParam_Volume,kAudioUnitScope_Global,0x00000001,.5f,0));
    CHECK(AudioUnitSetParameter(mixer,kMatrixMixerParam_Volume,kAudioUnitScope_Global,0x00010000,.25f,0));
    float left[16]={},right[16]={};
    struct {UInt32 n; AudioBuffer b[2];} data={2,{{1,sizeof(left),left},{1,sizeof(right),right}}};
    AudioTimeStamp time={}; time.mFlags=kAudioTimeStampSampleTimeValid;
    AudioUnitRenderActionFlags flags=0;
    CHECK(AudioUnitRender(mixer,&flags,&time,0,16,(AudioBufferList*)&data));
    for(unsigned f=0;f<16;++f) if(std::fabs(left[f]-.05f)>1e-6 || std::fabs(right[f]-.05f)>1e-6) {
        printf("FAIL matrix left=%f right=%f calls=%u flags=%u\n",left[f],right[f],calls.load(),flags); return 2;
    }
    CHECK(AudioUnitSetParameter(mixer,kMatrixMixerParam_Volume,kAudioUnitScope_Global,0xffffffff,0,0));
    time.mSampleTime=16; CHECK(AudioUnitRender(mixer,&flags,&time,0,16,(AudioBufferList*)&data));
    if (!(flags & kAudioUnitRenderAction_OutputIsSilence) || left[0] || right[0]) return 3;
    CHECK(AudioUnitSetParameter(mixer,kMatrixMixerParam_Volume,kAudioUnitScope_Global,0xffffffff,.05f,0));
    for(unsigned cycle=0;cycle<2;++cycle) {
        unsigned before=calls;
        CHECK(AUGraphStart(graph)); usleep(250000); CHECK(AUGraphStop(graph));
        if(calls<=before) {printf("FAIL no output callbacks\n");return 4;}
    }
    callbackGraph=graph;
    for(unsigned cycle=0;cycle<20;++cycle) {
        callbackStopResult=1;
        CHECK(AUGraphStart(graph));
        stopFromCallback=true;
        for(unsigned wait=0;wait<200 && callbackStopResult.load()==1;++wait) usleep(10000);
        if(callbackStopResult.load()!=noErr) {
            printf("FAIL stop inside render callback cycle=%u status=%d\n",cycle,callbackStopResult.load()); return 5;
        }
        // A stopped IOProc must not be invoked again, including queued PA work.
        unsigned stoppedCalls=calls.load(); usleep(50000);
        if(calls.load()!=stoppedCalls) {printf("FAIL callbacks after stop\n");return 6;}
    }
    CHECK(AUGraphUninitialize(graph)); CHECK(AUGraphClose(graph)); CHECK(DisposeAUGraph(graph));
    printf("PASS graph, matrix routing, mute, output start/stop/restart and 20 callback-initiated stops callbacks=%u\n",calls.load());
    return 0;
}
