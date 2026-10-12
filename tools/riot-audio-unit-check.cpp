#include <AudioToolbox/AudioUnit.h>
#include <CoreAudio/AudioHardware.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <unistd.h>

static std::atomic<unsigned> callbacks{0}, badBuffers{0}, maxFrames{0};
static unsigned phase;
static OSStatus render(void*,AudioUnitRenderActionFlags*,const AudioTimeStamp*,
    UInt32,UInt32 frames,AudioBufferList* buffers) {
    if(!buffers || buffers->mNumberBuffers!=2) {++badBuffers;return -1;}
    for(unsigned b=0;b<2;++b) {
        if(buffers->mBuffers[b].mNumberChannels!=1 ||
            buffers->mBuffers[b].mDataByteSize<frames*sizeof(float)) {++badBuffers;return -1;}
        auto* samples=(float*)buffers->mBuffers[b].mData;
        for(unsigned i=0;i<frames;++i) samples[i]=((phase+i)%100<50?.005f:-.005f);
    }
    phase+=frames;
    if(frames>maxFrames) maxFrames=frames;
    ++callbacks;
    return noErr;
}

int main() {
    AudioComponentDescription description={kAudioUnitType_Output,kAudioUnitSubType_HALOutput,
        kAudioUnitManufacturer_Apple,0,0};
    AudioComponent component=AudioComponentFindNext(nullptr,&description);
    printf("AUDIO_UNIT available=%d\n",component!=nullptr);fflush(stdout);
    if(!component) return 1;
    AudioComponentInstance unit=nullptr;
    OSStatus status=AudioComponentInstanceNew(component,&unit);
    if(status || !unit) return 2;
    UInt32 output=0xdeadbeef,size=sizeof(output);
    status=AudioUnitGetProperty(unit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Output,0,&output,&size);
    printf("AUDIO_UNIT_OUTPUT status=%d enabled=%u\n",status,output);fflush(stdout);
    if(status || output!=1) return 3;
    AudioChannelLayout layout={};size=sizeof(layout);
    status=AudioUnitGetProperty(unit,kAudioUnitProperty_AudioChannelLayout,kAudioUnitScope_Output,0,&layout,&size);
    printf("AUDIO_UNIT_LAYOUT status=%d stereo=%d\n",status,layout.mChannelLayoutTag==kAudioChannelLayoutTag_Stereo);fflush(stdout);
    if(status || layout.mChannelLayoutTag!=kAudioChannelLayoutTag_Stereo) return 9;
    AudioStreamBasicDescription format={44100,kAudioFormatLinearPCM,
        kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked|kAudioFormatFlagIsNonInterleaved,4,1,4,2,32,0};
    status=AudioUnitSetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format));
    printf("AUDIO_UNIT_CONFIGURE status=%d before_initialization=1\n",status);fflush(stdout);
    if(status) return 4;
    AURenderCallbackStruct callback={render,nullptr};
    status=AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback));
    if(status) return 5;
    UInt32 frames=256;
    status=AudioUnitSetProperty(unit,kAudioDevicePropertyBufferFrameSize,kAudioUnitScope_Global,0,&frames,sizeof(frames));
    if(status) return 6;
    status=AudioUnitInitialize(unit);
    printf("AUDIO_UNIT_INITIALIZE status=%d\n",status);fflush(stdout);
    if(status) return 7;
    status=AudioOutputUnitStart(unit);
    printf("AUDIO_UNIT_START status=%d\n",status);fflush(stdout);
    if(status) return 8;
    for(unsigned i=0;i<150;++i) usleep(20000);
    status=AudioOutputUnitStop(unit);
    printf("AUDIO_UNIT_RENDER callbacks=%u bad_buffers=%u max_frames=%u stop_status=%d\n",
        (unsigned)callbacks,(unsigned)badBuffers,(unsigned)maxFrames,status);fflush(stdout);
    for(unsigned cycle=0;cycle<2 && !status;cycle++) {
        unsigned before=callbacks;
        status=AudioOutputUnitStart(unit);
        for(unsigned i=0;i<50 && !status;++i) usleep(20000);
        OSStatus stopped=AudioOutputUnitStop(unit);
        printf("AUDIO_UNIT_RESTART cycle=%u start_status=%d stop_status=%d callbacks=%u\n",cycle+1,status,stopped,(unsigned)callbacks-before);fflush(stdout);
        if(!status) status=stopped;
        if(!status && callbacks==before) status=-1;
    }
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    return status || !callbacks || badBuffers || maxFrames>frames;
}
