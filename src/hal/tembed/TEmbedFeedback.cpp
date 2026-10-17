#include "TEmbedFeedback.h"

#include <Log.h>
#include <Preferences.h>

#include "Settings.h"

namespace
{
    constexpr const char *TAG = "FEEDBACK";
    constexpr i2s_port_t kPort = I2S_NUM_0;
    constexpr uint32_t kSampleRate = 16000;
    constexpr int16_t kAmplitude = 3000; // quiet, so the small speaker does not distort
    constexpr size_t kChunkFrames = 128;
    constexpr const char *kPrefSound = "sound";
} // namespace

TEmbedFeedback::TEmbedFeedback(uint8_t bclkPin, uint8_t lrclkPin, uint8_t doutPin, uint8_t ledPin, uint8_t ledCount,
                               uint8_t ledBrightness, bool soundDefaultOn)
    : bclkPin_(bclkPin), lrclkPin_(lrclkPin), doutPin_(doutPin), leds_(ledCount, ledPin, NEO_GRB + NEO_KHZ800),
      ledBrightness_(ledBrightness), soundDefaultOn_(soundDefaultOn)
{
}

void TEmbedFeedback::begin()
{
    leds_.begin();
    leds_.setBrightness(ledBrightness_);
    leds_.clear();
    leds_.show();

    // saved sound setting (default: kSoundDefaultOn)
    soundEnabled_ = soundDefaultOn_;
    Preferences prefs;
    if (prefs.begin(settings::kNvsNamespace, true))
    {
        soundEnabled_ = prefs.getBool(kPrefSound, soundDefaultOn_);
        prefs.end();
    }
    LOGI(TAG, "Sound %s", soundEnabled_ ? "on" : "off (LED feedback only)");

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = kSampleRate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true; // silence when not fed (the buffer is not repeated)

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = bclkPin_;
    pins.ws_io_num = lrclkPin_;
    pins.data_out_num = doutPin_;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    i2sOk_ = i2s_driver_install(kPort, &cfg, 0, nullptr) == ESP_OK && i2s_set_pin(kPort, &pins) == ESP_OK;
    if (i2sOk_)
        i2s_zero_dma_buffer(kPort);
    else
        LOGE(TAG, "I2S init failed - no sound, LED only");
}

void TEmbedFeedback::setSoundEnabled(bool on)
{
    soundEnabled_ = on;
    if (!on)
    {
        toneOff();
        if (i2sOk_)
            i2s_zero_dma_buffer(kPort);
    }
    Preferences prefs;
    if (prefs.begin(settings::kNvsNamespace, false))
    {
        prefs.putBool(kPrefSound, on);
        prefs.end();
    }
    LOGI(TAG, "Sound %s", on ? "on" : "off");
}

void TEmbedFeedback::toneOn(uint16_t freqHz)
{
    if (!soundEnabled_)
        return;
    freqHz_ = freqHz;
    phase_ = 0;
    service();
}

void TEmbedFeedback::toneOff()
{
    freqHz_ = 0;
}

void TEmbedFeedback::service()
{
    if (!i2sOk_ || freqHz_ == 0)
        return;

    // square wave; the DMA buffer is filled as far as it fits without blocking
    int16_t buf[kChunkFrames * 2];
    const uint32_t halfPeriod = kSampleRate / (2u * freqHz_);
    for (int chunk = 0; chunk < 4; chunk++)
    {
        for (size_t i = 0; i < kChunkFrames; i++)
        {
            const int16_t v = ((phase_ / (halfPeriod ? halfPeriod : 1)) & 1u) ? kAmplitude : -kAmplitude;
            buf[2 * i] = v;
            buf[2 * i + 1] = v;
            phase_++;
        }
        size_t written = 0;
        i2s_write(kPort, buf, sizeof(buf), &written, 0);
        if (written < sizeof(buf))
        {
            // step back by what did not fit, so the signal stays continuous
            phase_ -= (sizeof(buf) - written) / (2 * sizeof(int16_t));
            break;
        }
    }
}

void TEmbedFeedback::patternStarted(FeedbackPattern pattern)
{
    switch (pattern)
    {
    case FeedbackPattern::Startup: fill(leds_.Color(255, 255, 255)); break;
    case FeedbackPattern::CardAccepted: fill(leds_.Color(0, 80, 255)); break;
    case FeedbackPattern::Success: fill(leds_.Color(0, 255, 0)); break;
    case FeedbackPattern::Error: fill(leds_.Color(255, 0, 0)); break;
    }
}

void TEmbedFeedback::patternFinished()
{
    fill(0);
}

void TEmbedFeedback::allOff()
{
    toneOff();
    fill(0);
    if (i2sOk_)
        i2s_zero_dma_buffer(kPort);
}

void TEmbedFeedback::fill(uint32_t color)
{
    leds_.fill(color);
    leds_.show();
}
