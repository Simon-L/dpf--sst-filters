/*
 * ImGui plugin example
 * Copyright (C) 2021 Jean Pierre Cimalando <jp-dev@inbox.ru>
 * Copyright (C) 2021-2022 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoPlugin.hpp"
#include "CParamSmooth.hpp"

#include <memory>

#include <sst/filters.h>

// --------------------------------------------------------------------------------------------------------------------

#ifndef MIN
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#endif

#ifndef MAX
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#endif

#ifndef CLAMP
#define CLAMP(v, min, max) (MIN((max), MAX((min), (v))))
#endif

#ifndef DB_CO
#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g) * 0.05f) : 0.0f)
#endif

START_NAMESPACE_DISTRHO

// --------------------------------------------------------------------------------------------------------------------

class ImGuiPluginDSP : public Plugin
{
    enum Parameters {
        kParamGain = 0,
        kParamFreq,
        kParamCount
    };

    double fSampleRate = getSampleRate();
    float fGainDB = 0.0f;
    float fGainLinear = 1.0f;
    std::unique_ptr<CParamSmooth> fSmoothGain = std::make_unique<CParamSmooth>(20.0f, fSampleRate);

    float fFreqNote = 0.0f;
    sst::filters::FilterUnitQFPtr FUnit;

    sst::filters::FilterCoefficientMaker<> coeffMaker;
    sst::filters::QuadFilterUnitState filterState{};

    sst::filters::FilterType ft = sst::filters::FilterType::fut_lpmoog;
    sst::filters::FilterSubType fst = sst::filters::FilterSubType::st_lpmoog_6dB;

public:
   /**
      Plugin class constructor.@n
      You must set all parameter values to their defaults, matching ParameterRanges::def.
    */
    ImGuiPluginDSP()
        : Plugin(kParamCount, 0, 0) // parameters, programs, states
    {
        FUnit = sst::filters::GetQFPtrFilterUnit(ft, fst);
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Information

   /**
      Get the plugin label.@n
      This label is a short restricted name consisting of only _, a-z, A-Z and 0-9 characters.
    */
    const char* getLabel() const noexcept override
    {
        return "SimpleGain";
    }

   /**
      Get an extensive comment/description about the plugin.@n
      Optional, returns nothing by default.
    */
    const char* getDescription() const override
    {
        return "A simple audio volume gain plugin with ImGui for its GUI";
    }

   /**
      Get the plugin author/maker.
    */
    const char* getMaker() const noexcept override
    {
        return "Jean Pierre Cimalando, falkTX";
    }

   /**
      Get the plugin license (a single line of text or a URL).@n
      For commercial plugins this should return some short copyright information.
    */
    const char* getLicense() const noexcept override
    {
        return "ISC";
    }

   /**
      Get the plugin version, in hexadecimal.
      @see d_version()
    */
    uint32_t getVersion() const noexcept override
    {
        return d_version(1, 0, 0);
    }

   /**
      Get the plugin unique Id.@n
      This value is used by LADSPA, DSSI and VST plugin formats.
      @see d_cconst()
    */
    int64_t getUniqueId() const noexcept override
    {
        return d_cconst('d', 'I', 'm', 'G');
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Init

   /**
      Initialize the parameter @a index.@n
      This function will be called once, shortly after the plugin is created.
    */
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index) {
        case 0:
            parameter.ranges.min = -90.0f;
            parameter.ranges.max = 30.0f;
            parameter.ranges.def = -0.0f;
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Gain";
            parameter.shortName = "Gain";
            parameter.symbol = "gain";
            parameter.unit = "dB";
            break;
        case 1:
            parameter.ranges.min = -60.0f;
            parameter.ranges.max = 64.0f;
            parameter.ranges.def = -12.0f;
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "FrequencyNote";
            parameter.shortName = "FrequencyNote";
            parameter.symbol = "frequencynote";
            parameter.unit = "";
            break;
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Internal data

   /**
      Get the current value of a parameter.@n
      The host may call this function from any context, including realtime processing.
    */
    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case 0:
            return fGainDB;
        case 1:
            return fFreqNote;
        default:
            return 0.0;
        }
    }

   /**
      Change a parameter value.@n
      The host may call this function from any context, including realtime processing.@n
      When a parameter is marked as automatable, you must ensure no non-realtime operations are performed.
      @note This function will only be called for parameter inputs.
    */
    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case 0:
            fGainDB = value;
            fGainLinear = DB_CO(CLAMP(value, -90.0, 30.0));
            break;
        case 1:
            fFreqNote = value;
            d_stdout("New freq note: %f", fFreqNote);
            coeffMaker.MakeCoeffs(fFreqNote, 0.2, ft, fst, nullptr, false);
            coeffMaker.updateState(filterState);
            break;
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Audio/MIDI Processing

   /**
      Activate this plugin.
    */
    void activate() override
    {
        fSmoothGain->flush();

        coeffMaker.setSampleRateAndBlockSize((float)getSampleRate(), getBufferSize());
        coeffMaker.MakeCoeffs(-12.0, 0.2, ft, fst, nullptr, false);
        coeffMaker.updateState(filterState);
    }

    uint64_t time;
    float timef;
    const float sin_prec = 2 * M_PI * 440.0f;
   /**
      Run/process function for plugins without MIDI input.
      @note Some parameters might be null if there are no audio inputs or outputs.
    */
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {   
        // get the left and right audio inputs
        const float* const inpL = inputs[0];
        const float* const inpR = inputs[1];

        // get the left and right audio outputs
        float* const outL = outputs[0];
        float* const outR = outputs[1];

        // apply gain against all samples
        float s;
        for (uint32_t i=0; i < frames; ++i)
        {   

            time++;
            timef = time / getSampleRate();
            s = 0.25 * std::sin(sin_prec * timef);
            const float gain = fSmoothGain->process(fGainLinear);

            auto yVec = FUnit(&filterState, _mm_set_ps1(s));

            float yArr alignas(16)[4];
            _mm_store_ps (yArr, yVec);
            outL[i] = yArr[0] * gain; // out L is filtered
            outR[i] = s * gain; // out L is unfilterd
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Callbacks (optional)

   /**
      Optional callback to inform the plugin about a sample rate change.@n
      This function will only be called when the plugin is deactivated.
      @see getSampleRate()
    */
    void sampleRateChanged(double newSampleRate) override
    {
        fSampleRate = newSampleRate;
        fSmoothGain->setSampleRate(newSampleRate);
    }

    // ----------------------------------------------------------------------------------------------------------------

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImGuiPluginDSP)
};

// --------------------------------------------------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new ImGuiPluginDSP();
}

// --------------------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
