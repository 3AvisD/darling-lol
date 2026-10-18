// Microphone path used by voice chat (Vivox/Chromium): AVFoundation audio authorization, the
// input device's channels, and an AUHAL input unit delivering recorded frames through
// AudioUnitRender(bus 1). Samples are only counted, never stored or printed.
#include <AudioToolbox/AudioUnit.h>
#include <CoreAudio/AudioHardware.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

extern "C" void* const AVMediaTypeAudio;
extern "C" void* const AVMediaTypeVideo;

static AudioUnit unit;
static std::atomic<unsigned> callbacks{0}, renderErrors{0}, nonIncreasing{0}, zeroFrames{0}, badBuffers{0};
static std::atomic<unsigned long long> totalFrames{0};
static double lastSample=-1;
static bool planar;
static unsigned interleavedChannels=1;
static float samples[2][2*4096];

static OSStatus input(void*,AudioUnitRenderActionFlags* flags,const AudioTimeStamp* time,UInt32 bus,UInt32 frames,AudioBufferList*) {
    ++callbacks;
    if(!frames || frames>4096) {++zeroFrames;return noErr;}
    if(!(time->mFlags&kAudioTimeStampSampleTimeValid) || time->mSampleTime<=lastSample) ++nonIncreasing;
    lastSample=time->mSampleTime;
    unsigned char storage[sizeof(AudioBufferList)+sizeof(AudioBuffer)];
    auto* list=(AudioBufferList*)storage;
    list->mNumberBuffers=planar?2:1;
    for(unsigned b=0;b<list->mNumberBuffers;++b) {
        list->mBuffers[b].mNumberChannels=planar?1:interleavedChannels;
        list->mBuffers[b].mDataByteSize=frames*sizeof(float)*list->mBuffers[b].mNumberChannels;
        list->mBuffers[b].mData=samples[b];
    }
    AudioUnitRenderActionFlags renderFlags=0;
    OSStatus status=AudioUnitRender(unit,&renderFlags,time,bus,frames,list);
    if(status) ++renderErrors;
    else for(unsigned b=0;b<list->mNumberBuffers;++b)
        if(list->mBuffers[b].mDataByteSize!=frames*sizeof(float)*list->mBuffers[b].mNumberChannels) ++badBuffers;
    totalFrames+=frames;
    return noErr;
}

static int authorization() {
    Class device=objc_getClass("AVCaptureDevice");
    if(!device) {printf("AUDIO_INPUT_AUTH class=0\n");return 1;}
    SEL status=sel_registerName("authorizationStatusForMediaType:");
    auto query=(long(*)(id,SEL,void*))objc_msgSend;
    long audio=query((id)device,status,AVMediaTypeAudio),video=query((id)device,status,AVMediaTypeVideo);
    // macOS calls the handler on another queue, never on the requesting thread inside the call.
    // Shared with the handler's thread, so atomic; read only after the handler signals.
    static std::atomic<int> granted{-1};
    static std::atomic<bool> synchronous{false};
    const pthread_t caller=pthread_self();
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    auto request=(void(*)(id,SEL,void*,void(^)(BOOL)))objc_msgSend;
    request((id)device,sel_registerName("requestAccessForMediaType:completionHandler:"),AVMediaTypeAudio,^(BOOL value) {
        synchronous=pthread_equal(pthread_self(),caller)!=0;granted=value;dispatch_semaphore_signal(done);
    });
    long waited=dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC));
    printf("AUDIO_INPUT_AUTH audio=%ld video=%ld request_granted=%d async=%d\n",audio,video,(int)granted,!waited && !synchronous);
    fflush(stdout);
    return audio!=3 || video!=2 || waited || granted!=1 || synchronous;
}

static int device(AudioDeviceID* out) {
    AudioObjectPropertyAddress p={kAudioHardwarePropertyDefaultInputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    AudioDeviceID id=kAudioObjectUnknown;UInt32 size=sizeof(id);
    OSStatus status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&p,0,nullptr,&size,&id);
    if(status || id==kAudioObjectUnknown) {printf("AUDIO_INPUT_DEVICE status=%d\n",status);return 1;}
    unsigned channels[2]={0,0};
    for(unsigned i=0;i<2;++i) {
        AudioObjectPropertyAddress c={kAudioDevicePropertyStreamConfiguration,
            i?kAudioObjectPropertyScopeOutput:kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMaster};
        unsigned char storage[256]={};size=sizeof(storage);
        if(AudioObjectGetPropertyData(id,&c,0,nullptr,&size,storage)) return 2;
        auto* list=(AudioBufferList*)storage;
        for(unsigned b=0;b<list->mNumberBuffers;++b) channels[i]+=list->mBuffers[b].mNumberChannels;
    }
    AudioObjectPropertyAddress s={kAudioDevicePropertyStreams,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMaster};
    UInt32 streams=0;
    status=AudioObjectGetPropertyDataSize(id,&s,0,nullptr,&streams);
    AudioObjectPropertyAddress d={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    AudioDeviceID devices[16];size=sizeof(devices);
    bool listed=false;
    if(!AudioObjectGetPropertyData(kAudioObjectSystemObject,&d,0,nullptr,&size,devices))
        for(unsigned i=0;i<size/sizeof(AudioDeviceID);++i) listed|=devices[i]==id;
    printf("AUDIO_INPUT_DEVICE input_channels=%u output_channels=%u input_streams=%u listed=%d\n",
        channels[0],channels[1],(unsigned)(streams/sizeof(AudioStreamID)),listed);
    fflush(stdout);
    *out=id;
    return channels[0]!=2 || channels[1]!=0 || status || !streams || !listed;
}

static int record(AudioDeviceID id,bool nonInterleaved,unsigned channels,unsigned milliseconds) {
    AudioComponentDescription description={kAudioUnitType_Output,kAudioUnitSubType_HALOutput,kAudioUnitManufacturer_Apple,0,0};
    AudioComponent component=AudioComponentFindNext(nullptr,&description);
    if(!component || AudioComponentInstanceNew(component,&unit) || !unit) return 1;
    planar=nonInterleaved;interleavedChannels=channels;callbacks=0;renderErrors=0;nonIncreasing=0;zeroFrames=0;badBuffers=0;totalFrames=0;lastSample=-1;
    UInt32 on=1,off=0;
    if(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Input,1,&on,sizeof(on))) return 2;
    if(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Output,0,&off,sizeof(off))) return 3;
    // Vivox (the interleaved stereo run) sets the capture device on element 1.
    const AudioUnitElement deviceElement=channels==2?1:0;
    if(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,deviceElement,&id,sizeof(id))) return 4;
    AudioDeviceID current=0;UInt32 size=sizeof(current);
    if(AudioUnitGetProperty(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,deviceElement,&current,&size) || current!=id) return 5;
    AudioStreamBasicDescription format=nonInterleaved
        ? AudioStreamBasicDescription{44100,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked|kAudioFormatFlagIsNonInterleaved,4,1,4,2,32,0}
        : channels==2
        // Vivox's recording format: interleaved Float32 stereo at the device rate.
        ? AudioStreamBasicDescription{44100,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,8,1,8,2,32,0}
        : AudioStreamBasicDescription{48000,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,4,1,4,1,32,0};
    OSStatus status=AudioUnitSetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,1,&format,sizeof(format));
    if(status) {printf("AUDIO_INPUT_FORMAT status=%d\n",status);return 6;}
    UInt32 frames=480;
    if(AudioUnitSetProperty(unit,kAudioDevicePropertyBufferFrameSize,kAudioUnitScope_Global,0,&frames,sizeof(frames))) return 7;
    AURenderCallbackStruct callback={input,nullptr};
    if(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_SetInputCallback,kAudioUnitScope_Global,0,&callback,sizeof(callback))) return 8;
    if(AudioUnitInitialize(unit)) return 9;
    status=AudioOutputUnitStart(unit);
    if(status) {printf("AUDIO_INPUT_START status=%d\n",status);return 10;}
    usleep(milliseconds*1000);
    OSStatus stopped=AudioOutputUnitStop(unit);
    unsigned count=callbacks;
    usleep(100000);
    bool quiet=callbacks==count;
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    double seconds=double(totalFrames)/format.mSampleRate;
    printf("AUDIO_INPUT_RECORD planar=%d channels=%u rate=%.0f callbacks=%u frames=%llu seconds=%.3f render_errors=%u bad_buffers=%u "
        "nonincreasing=%u zero_frames=%u stop_status=%d quiet_after_stop=%d\n",nonInterleaved,(unsigned)format.mChannelsPerFrame,format.mSampleRate,count,
        (unsigned long long)totalFrames,seconds,(unsigned)renderErrors,(unsigned)badBuffers,(unsigned)nonIncreasing,
        (unsigned)zeroFrames,stopped,quiet);
    fflush(stdout);
    // Startup latency aside, most of the recording time must have been delivered.
    return !count || renderErrors || badBuffers || nonIncreasing || zeroFrames || stopped || !quiet
        || seconds<milliseconds/2000.0;
}

// Vivox's playback unit sets interleaved Float32 stereo at 32 kHz on input scope element 0;
// only the format is checked, nothing is played.
static int playbackFormat() {
    AudioComponentDescription description={kAudioUnitType_Output,kAudioUnitSubType_HALOutput,kAudioUnitManufacturer_Apple,0,0};
    AudioComponent component=AudioComponentFindNext(nullptr,&description);
    AudioUnit playback=nullptr;
    if(!component || AudioComponentInstanceNew(component,&playback) || !playback) return 1;
    AudioStreamBasicDescription format={32000,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,8,1,8,2,32,0};
    OSStatus status=AudioUnitSetProperty(playback,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format));
    AudioStreamBasicDescription unsignedFormat={44100,kAudioFormatLinearPCM,kAudioFormatFlagIsPacked,4,1,4,2,16,0};
    OSStatus rejected=AudioUnitSetProperty(playback,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&unsignedFormat,sizeof(unsignedFormat));
    AudioComponentInstanceDispose(playback);
    printf("AUDIO_PLAYBACK_FORMAT interleaved_status=%d unsigned_status=%d\n",(int)status,(int)rejected);
    fflush(stdout);
    return status!=noErr || rejected==noErr;
}

int main() {
    int failures=0;
    failures+=authorization()!=0;
    AudioDeviceID id=kAudioObjectUnknown;
    if(device(&id)) return 100+failures;
    int result=record(id,false,1,1000);
    if(result) printf("AUDIO_INPUT_RECORD failed step=%d\n",result);
    failures+=result!=0;
    result=record(id,true,2,400);
    if(result) printf("AUDIO_INPUT_RECORD failed step=%d planar=1\n",result);
    failures+=result!=0;
    result=record(id,false,2,400);
    if(result) printf("AUDIO_INPUT_RECORD failed step=%d interleaved_stereo=1\n",result);
    failures+=result!=0;
    failures+=playbackFormat()!=0;
    printf("AUDIO_INPUT_CHECK failures=%d\n",failures);
    return failures;
}
