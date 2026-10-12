#include <CoreAudio/AudioHardware.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <CoreFoundation/CFRunLoop.h>

static _Atomic unsigned callbacks;
static unsigned phase;
static OSStatus tone(AudioDeviceID device,const AudioTimeStamp* now,
    const AudioBufferList* input,const AudioTimeStamp* inputTime,
    AudioBufferList* output,const AudioTimeStamp* outputTime,void* user) {
    (void)device;(void)now;(void)input;(void)inputTime;(void)outputTime;(void)user;
    for(unsigned b=0;b<output->mNumberBuffers;++b) {
        float *samples=output->mBuffers[b].mData;
        unsigned channels=output->mBuffers[b].mNumberChannels;
        unsigned count=output->mBuffers[b].mDataByteSize/sizeof(float);
        for(unsigned i=0;i<count;i+=channels) {
            float value=((phase++%100)<50?.01f:-.01f);
            for(unsigned c=0;c<channels && i+c<count;++c) samples[i+c]=value;
        }
    }
    ++callbacks;
    return noErr;
}

int main(int argc,char **argv) {
    AudioObjectPropertyAddress p={kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    AudioDeviceID device=0; UInt32 size=sizeof(device);
    OSStatus status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&p,0,0,&size,&device);
    printf("AUDIO_DEFAULT status=%d available=%d\n",status,device!=0);
    if(status || !device) return 1;
    p.mSelector=kAudioDevicePropertyDeviceUID;
    CFStringRef uid=0;size=sizeof(uid);
    status=AudioObjectGetPropertyData(device,&p,0,0,&size,&uid);
    if(status || !uid) return 9;
    AudioDeviceID translated=0;
    AudioValueTranslation translation={&uid,sizeof(uid),&translated,sizeof(translated)};
    p.mSelector=kAudioHardwarePropertyDeviceForUID;size=sizeof(translation);
    status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&p,0,0,&size,&translation);
    CFRelease(uid);
    printf("AUDIO_UID_TRANSLATION status=%d matches_default=%d\n",status,translated==device);
    if(status || translated!=device) return 10;
    p.mSelector=kAudioDevicePropertyPreferredChannelLayout;p.mScope=kAudioObjectPropertyScopeOutput;
    size=0;status=AudioObjectGetPropertyDataSize(device,&p,0,0,&size);
    if(status || size!=offsetof(AudioChannelLayout,mChannelDescriptions)) return 11;
    AudioChannelLayout layout={0};size=sizeof(layout);
    status=AudioObjectGetPropertyData(device,&p,0,0,&size,&layout);
    printf("AUDIO_PREFERRED_LAYOUT status=%d stereo=%d size=%u\n",status,layout.mChannelLayoutTag==kAudioChannelLayoutTag_Stereo,size);
    if(status || layout.mChannelLayoutTag!=kAudioChannelLayoutTag_Stereo || layout.mNumberChannelDescriptions) return 12;
    unsigned char small[12];memset(small,0xa5,sizeof(small));size=11;
    status=AudioObjectGetPropertyData(device,&p,0,0,&size,small);
    if(status!=kAudioHardwareBadPropertySizeError || small[0]!=0xa5 || small[11]!=0xa5) return 13;
    UInt32 stereo[2]={0};size=sizeof(stereo);p.mSelector=kAudioDevicePropertyPreferredChannelsForStereo;
    status=AudioObjectGetPropertyData(device,&p,0,0,&size,stereo);
    if(status || stereo[0]!=1 || stereo[1]!=2) return 14;
    AudioObjectPropertyAddress runloop={kAudioHardwarePropertyRunLoop,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    CFRunLoopRef loop=NULL;
    status=AudioObjectSetPropertyData(kAudioObjectSystemObject,&runloop,0,0,sizeof(loop),&loop);
    if(status) return 15;
    size=sizeof(loop);loop=(CFRunLoopRef)(uintptr_t)1;
    status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&runloop,0,0,&size,&loop);
    if(status || loop) return 16;
    status=AudioObjectSetPropertyData(kAudioObjectSystemObject,&runloop,0,0,1,&loop);
    if(status!=kAudioHardwareBadPropertySizeError) return 17;
    p.mSelector=kAudioDevicePropertyStreams;p.mScope=kAudioObjectPropertyScopeOutput;
    size=0;status=AudioObjectGetPropertyDataSize(device,&p,0,0,&size);
    printf("AUDIO_OUTPUT_STREAMS status=%d count=%u\n",status,size/(unsigned)sizeof(AudioStreamID));
    if(status || size<sizeof(AudioStreamID)) return 2;
    AudioStreamID *streams=malloc(size);
    status=AudioObjectGetPropertyData(device,&p,0,0,&size,streams);
    if(status) return 3;
    p.mSelector=kAudioStreamPropertyDirection;p.mScope=kAudioObjectPropertyScopeGlobal;
    UInt32 direction=99;size=sizeof(direction);
    status=AudioObjectGetPropertyData(streams[0],&p,0,0,&size,&direction);
    free(streams);
    printf("AUDIO_OUTPUT_DIRECTION status=%d output=%d\n",status,direction==0);
    if(status || direction!=0) return 4;
    p.mSelector=kAudioDevicePropertyStreamFormat;p.mScope=kAudioObjectPropertyScopeOutput;
    AudioStreamBasicDescription format={0};size=sizeof(format);
    status=AudioObjectGetPropertyData(device,&p,0,0,&size,&format);
    printf("AUDIO_FRAME_FORMAT status=%d channels=%u bytes_per_frame=%u rate=%.0f\n",
        status,format.mChannelsPerFrame,format.mBytesPerFrame,format.mSampleRate);
    if(status || format.mBytesPerFrame!=format.mChannelsPerFrame*format.mBitsPerChannel/8) return 5;
    if(argc>1 && !strcmp(argv[1],"--tone")) {
        AudioDeviceIOProcID proc=0;
        status=AudioDeviceCreateIOProcID(device,tone,0,&proc);
        if(status) return 6;
        status=AudioDeviceStart(device,proc);
        printf("AUDIO_START status=%d\n",status);fflush(stdout);
        if(status) {AudioDeviceDestroyIOProcID(device,proc);return 7;}
        for(unsigned i=0;i<150;++i) usleep(20000);
        printf("AUDIO_CALLBACKS count=%u\n",(unsigned)callbacks);fflush(stdout);
        OSStatus stopped=AudioDeviceStop(device,proc);
        AudioDeviceDestroyIOProcID(device,proc);
        printf("AUDIO_STOP status=%d\n",stopped);
        if(stopped || !callbacks) return 8;
    }
    return 0;
}
