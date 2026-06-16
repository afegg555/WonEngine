#include "AudioDriverXAudio2.h"
#include "Backlog.h"

#include <memory>

namespace won::audio
{
    AudioDriverXAudio2::~AudioDriverXAudio2()
    {
        Stop();
    }

    bool AudioDriverXAudio2::Start(const AudioDriverDesc& in_desc, MixCallback callback, void* userdata)
    {
        desc = in_desc;
        mix_callback = callback;
        user_data = userdata;

		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); // initialize COM. this is ok to call multiple times eg.filesystem, dxgi, etc.
        if (FAILED(hr) && hr != S_FALSE)
        {
            wonlog_error("[AudioDriver] CoInitializeEx failed.");
            return false;
        }

		hr = XAudio2Create(&xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR); // XAudio main interface instance
        if (FAILED(hr) || !xaudio2)
        {
            wonlog_error("[AudioDriver] XAudio2Create failed.");
            return false;
        }

		hr = xaudio2->CreateMasteringVoice(&mastering_voice); // audio output device
        if (FAILED(hr))
        {
            wonlog_error("[AudioDriver] CreateMasteringVoice failed.");
            return false;
        }

        const uint32 samples_per_buffer = desc.buffer_frames * desc.channel_count;
        for (auto& buf : audio_buffers)
            buf.assign(samples_per_buffer, 0.0f);

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wfx.nChannels = static_cast<WORD>(desc.channel_count);
        wfx.nSamplesPerSec = desc.sample_rate;
        wfx.wBitsPerSample = 32;
        wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

		hr = xaudio2->CreateSourceVoice(&source_voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this); // callback is this class
        if (FAILED(hr))
        {
            wonlog_error("[AudioDriver] CreateSourceVoice failed.");
            return false;
        }

        is_running = true;

        SubmitBuffer(0);
        SubmitBuffer(1);

        source_voice->Start(0);
        return true;
    }

    void AudioDriverXAudio2::Stop()
    {
        is_running = false;

        if (source_voice)
        {
            source_voice->Stop(0);
            source_voice->FlushSourceBuffers();
            source_voice->DestroyVoice();
            source_voice = nullptr;
        }

        if (mastering_voice)
        {
            mastering_voice->DestroyVoice();
            mastering_voice = nullptr;
        }

        if (xaudio2)
        {
            xaudio2->Release();
            xaudio2 = nullptr;
        }

        CoUninitialize();
    }

    void AudioDriverXAudio2::SubmitBuffer(uint32 buffer_index)
    {
        Vector<float>& buf = audio_buffers[buffer_index];

        if (mix_callback)
            mix_callback(buf.data(), desc.buffer_frames, user_data);

        XAUDIO2_BUFFER xbuf = {};
        xbuf.AudioBytes = static_cast<UINT32>(buf.size() * sizeof(float));
        xbuf.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
        xbuf.pContext = reinterpret_cast<void*>(static_cast<uintptr_t>(buffer_index));

        source_voice->SubmitSourceBuffer(&xbuf);
    }

    void STDMETHODCALLTYPE AudioDriverXAudio2::OnBufferEnd(void* pBufferContext)
    {
        if (!is_running)
            return;

        const uint32 finished_index = static_cast<uint32>(reinterpret_cast<uintptr_t>(pBufferContext));
        // In triple buffering, the next buffer to submit is 2 steps ahead of the finished one
        // (finished + 1 is currently playing, so finished + 2 is the free one).
        const uint32 next_index = (finished_index + 2) % buffer_count;
        SubmitBuffer(next_index);
    }

}
