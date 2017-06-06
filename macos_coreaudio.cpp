/**
 * \file
 *
 * Play a stereo audio stream using Apple's HAL layer.
 *
 * Apple tends to recommend to use the AUHAL audio unit however since
 * the stream we are using is fairly standard, most hardware would
 * support it and we stay as close as we can to the hardware layer
 * this way.
 */

#include "macos_coreaudio.hpp"

#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/CoreAudioTypes.h>

#include <mach/mach_time.h>

//! what are the selected channels for our stereo stream
struct StereoChannelDesc
{
  int channels[2];
  struct Clock* clock;
};

struct CoreaudioStreamResources
{
  AudioDeviceID DeviceID;
  AudioDeviceIOProcID IOProcID;
};

struct CoreaudioStreamValue
{
  CoreaudioStreamHeader header;
  StereoChannelDesc selected_channels;
};
static_assert(sizeof (CoreaudioStreamValue) < sizeof (CoreaudioStream), "opaque representation");

static OSStatus audio_callback(
    AudioDeviceID           inDevice,
    const AudioTimeStamp*   inNow,
    const AudioBufferList*  inInputData,
    const AudioTimeStamp*   inInputTime,
    AudioBufferList*        outOutputData,
    const AudioTimeStamp*   inOutputTime,
    void*                   inClientData);

static CoreaudioStreamResources macos_coreaudio_stream_resources;
static mach_timebase_info_data_t macos_coreaudio_mach_timebase_info;

#define HW_PROPERTY_ADDRESS(key)                                        \
  { key, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster, }

#define HW_OUTPUT_PROPERTY_ADDRESS(key)                                 \
  { key, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster, }

#define OS_SUCCESS(call) ((call) == noErr)
#define THEN_DO(expr) ((expr), 1)

#define FAIL_WITH(...) (THEN_DO(printf(__VA_ARGS__)) && 0)
#define BREAK_ON_ERROR(expr)                    \
  if (!(expr)) {                                \
    return;                                     \
  }

static void
macos_coreaudio_close_resources(CoreaudioStreamResources* _resources);

void macos_coreaudio_stream_close_atexit()
{
  macos_coreaudio_close_resources(&macos_coreaudio_stream_resources);
}

CoreaudioStreamError
macos_coreaudio_open_stereo(CoreaudioStream* _stream, int audio_hz)
{
  CoreaudioStreamValue stream;
  memcpy(&stream, _stream, sizeof stream);
  stream.header.error = CoreaudioStreamError_SystemError;

  auto &channel_desc = stream.selected_channels;
  double const preferred_hz = audio_hz;
  OSStatus status = noErr;

  if (macos_coreaudio_stream_resources.DeviceID ||
      macos_coreaudio_stream_resources.IOProcID) {
    stream.header.error_string = "can't open more than one stream";
    goto error;
  }
  
  if (mach_timebase_info(&macos_coreaudio_mach_timebase_info)) {
    stream.header.error_string = "could not obtain timebase";
    goto error;
  }

  if (!stream.header.input_render) {
    stream.header.error_string = "missing input_render function";
    goto error;
  }
  
  AudioDeviceID outputDevice;
  {
    UInt32 size = sizeof outputDevice;
    AudioObjectPropertyAddress const address =
      HW_PROPERTY_ADDRESS(kAudioHardwarePropertyDefaultOutputDevice);

    status = AudioObjectGetPropertyData
      (kAudioObjectSystemObject, &address, 0, NULL, &size, &outputDevice);
    if (status != noErr || outputDevice == 0) {
      stream.header.error_string = "could not get default output device";
      goto error;
    }
  }

  UInt32 left_right_channels[2];
  {
    UInt32 property_size;
    AudioObjectPropertyAddress property;

    property_size = sizeof left_right_channels;
    property = HW_OUTPUT_PROPERTY_ADDRESS
      (kAudioDevicePropertyPreferredChannelsForStereo);

    status = AudioObjectGetPropertyData
      (outputDevice,
       &property,
       0, NULL,
       &property_size,
       &left_right_channels);
    if (status != noErr) {
      stream.header.error_string = "could not query stereo channels";
      goto error;
    }
  }

  {
    AudioObjectPropertyAddress streams_address =
      HW_OUTPUT_PROPERTY_ADDRESS(kAudioDevicePropertyStreams);
    UInt32 streams_size;

    status = AudioObjectGetPropertyDataSize
      (outputDevice,
       &streams_address,
       0,
       NULL,
       &streams_size);
    if (status != noErr) {
      stream.header.error_string = "could not query streams size";
      goto error;
    }
    
    char streams_buffer[streams_size];
    AudioStreamID* streams = (AudioStreamID*) streams_buffer;
    int const streams_n = streams_size / sizeof streams[0];
    
    status = AudioObjectGetPropertyData
      (outputDevice,
       &streams_address,
       0,
       NULL,
       &streams_size,
       streams);
    if (status != noErr) {
      stream.header.error_string = "could not query streams";
    }

    for (int streams_i = 0; streams_i < streams_n; streams_i++) {
      AudioStreamID candidate_stream = streams[streams_i];
      
      UInt32 starting_channel;
      {
        UInt32 size = sizeof starting_channel;
        AudioObjectPropertyAddress const address =
          HW_PROPERTY_ADDRESS(kAudioStreamPropertyStartingChannel);

        status = AudioObjectGetPropertyData
                         (candidate_stream,
                          &address,
                          0,
                          NULL,
                          &size,
                          &starting_channel);
        if (status != noErr) {
          stream.header.error_string = "could not query starting channel";
          continue;
        }
        if (starting_channel != left_right_channels[0] &&
            starting_channel != left_right_channels[1]) {
          continue;
        }
      }

      UInt32 property_size;
      AudioObjectPropertyAddress const property_address =
        HW_PROPERTY_ADDRESS(kAudioStreamPropertyAvailableVirtualFormats);

      status = AudioObjectGetPropertyDataSize
        (candidate_stream,
         &property_address,
         0,
         NULL,
         &property_size);
      if (status != noErr) {
        stream.header.error_string = "could not get formats size";
        goto error;
      }

      char buffer[property_size];
      AudioStreamRangedDescription* descriptions =
        (AudioStreamRangedDescription*) buffer;
      int const descriptions_n = property_size / sizeof descriptions[0];

      status = AudioObjectGetPropertyData
        (candidate_stream,
         &property_address,
         0,
         NULL,
         &property_size,
         descriptions);
      if (status != noErr) {
        stream.header.error_string = "could not get formats";
        goto error;
      }

      int i;
      for (i = 0; i < descriptions_n; i++) {
        AudioStreamRangedDescription desc = descriptions[i];
        if (desc.mFormat.mFormatID != kAudioFormatLinearPCM) {
          continue;
        }
        
        bool is_variadic = kAudioStreamAnyRate == desc.mFormat.mSampleRate &&
          preferred_hz >= desc.mSampleRateRange.mMinimum &&
          preferred_hz <= desc.mSampleRateRange.mMaximum;
        if (!is_variadic &&
            fabs(preferred_hz - desc.mFormat.mSampleRate) >= 0.01) {
          continue;
        }
        
        if (desc.mFormat.mFormatFlags !=
            (kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked)) {
          continue;
        }
        
        desc.mFormat.mSampleRate = preferred_hz;
        UInt32 property_size = sizeof desc.mFormat;
        AudioObjectPropertyAddress const property_address =
          HW_PROPERTY_ADDRESS(kAudioStreamPropertyVirtualFormat);

        status = AudioObjectSetPropertyData
          (candidate_stream,
           &property_address,
           0,
           NULL,
           property_size,
           &desc.mFormat);
        if (status != noErr) {
          stream.header.error_string = "could not set stream format";
          goto error;
        }
          
        /* mark the channel as configured */
        if (desc.mFormat.mChannelsPerFrame < 2) {
          if (left_right_channels[0] == starting_channel) {
            channel_desc.channels[0] = starting_channel;
            left_right_channels[0] = 0;
          } else if (left_right_channels[1] == starting_channel) {
            channel_desc.channels[1] = starting_channel;
            left_right_channels[1] = 0;
          }
        } else {
          channel_desc.channels[0] = left_right_channels[0];
          channel_desc.channels[1] = left_right_channels[1];
          
          left_right_channels[0] = 0;
          left_right_channels[1] = 0;
        }
        break;
      }
    }
    
    if (left_right_channels[0] != 0 ||
        left_right_channels[1] != 0) {
      stream.header.error_string = "could not select channels";
      goto error;
    }

    {
      AudioDeviceIOProcID procID;
      status = AudioDeviceCreateIOProcID
        (outputDevice,
         audio_callback,
         _stream,
         &procID);
      if (status != noErr) {
        stream.header.error_string = "could not create AudioIOProc";
        goto error;
      }

      macos_coreaudio_stream_resources.DeviceID = outputDevice;
      macos_coreaudio_stream_resources.IOProcID = procID;

      memcpy(_stream, &stream, sizeof stream);
      status = AudioDeviceStart(outputDevice, procID);
      if (status != noErr) {
        stream.header.error_string = "could not start output device";
        goto error;
      }
      stream.header.error = CoreaudioStreamError_Success;
    }
    atexit(macos_coreaudio_stream_close_atexit);
    return stream.header.error;
  }
  
 error:
  memcpy(_stream, &stream, sizeof stream);
  return stream.header.error;
}
  
static OSStatus audio_callback(AudioDeviceID    /*inDevice*/,
                        const AudioTimeStamp*   /*inNow*/,
                        const AudioBufferList*  /*inInputData*/,
                        const AudioTimeStamp*   /*inInputTime*/,
                        AudioBufferList*        outOutputData,
                        const AudioTimeStamp*   inOutputTime,
                        void*                   inClientData)
{
  CoreaudioStreamValue stream;
  memcpy(&stream, inClientData, sizeof stream);
  auto const selected_channels = stream.selected_channels;

  struct {
    float* buffer;
    int stride;
    int frame_count;
  } output[2] = {
    { NULL, 1, 0 },
    { NULL, 1, 0 },
  };

  // find buffers to bind our channels to
  int current_channel = 1;
  for (UInt32 i = 0; i < outOutputData->mNumberBuffers; i++) {
    AudioBuffer* const buffer = &outOutputData->mBuffers[i];
    float* const samples = static_cast<float*> (buffer->mData);
    int const frame_count =
      buffer->mDataByteSize / buffer->mNumberChannels / sizeof samples[0];
    int const stride = buffer->mNumberChannels;

    for (UInt32 j = 0; j < buffer->mNumberChannels; j++) {
      if (samples) {
        for (int oi = 0; oi < 2; oi++) {
          if (current_channel == selected_channels.channels[oi]) {
            output[oi].buffer = &samples[j];
            output[oi].stride = stride;
            output[oi].frame_count = frame_count;
          }
        }
      }
      current_channel++;
    }
  }

  if (output[0].frame_count != output[1].frame_count) {
    printf("err: %d is not %d\n", output[0].frame_count, output[1].frame_count);
    return noErr;
  }

  if (!output[0].buffer || !output[1].buffer) {
    printf("err: no left/right buffer\n");
    return noErr;
  }

  // ask our client to generate content into temporary buffers
  int const frame_count = output[0].frame_count;
  float stereo_client_buffer[2*frame_count];

  memset(stereo_client_buffer, 0, sizeof stereo_client_buffer);

  uint64_t clock_micros;
  {
    auto const timebase = macos_coreaudio_mach_timebase_info;
    clock_micros = (inOutputTime->mHostTime - stream.header.mach_absolute_time_origin) *
      timebase.numer / timebase.denom / 1000;
  }

  stream.header.input_render
    (clock_micros,
     stereo_client_buffer,
     frame_count);

  for (int i = 0; i < frame_count; i++) {
    output[0].buffer[i * output[0].stride] = (float) stereo_client_buffer[2*i];
  }

  for (int i = 0; i < frame_count; i++) {
    output[1].buffer[i * output[1].stride] = (float) stereo_client_buffer[2*i+1];
  }

  return noErr;
}

static void
macos_coreaudio_close_resources(CoreaudioStreamResources* _resources)
{
  auto &resources = *_resources;
  if (resources.DeviceID && resources.IOProcID) {
    (void) (OS_SUCCESS(AudioDeviceStop(resources.DeviceID, audio_callback))
            && OS_SUCCESS(AudioDeviceDestroyIOProcID(resources.DeviceID, resources.IOProcID))
            || FAIL_WITH("could not close device"));
    resources.DeviceID = 0;
    resources.IOProcID = 0;
    printf("closed stream\n");
  }
}

void macos_coreaudio_close(CoreaudioStream*)
{
  macos_coreaudio_close_resources(&macos_coreaudio_stream_resources);
}
