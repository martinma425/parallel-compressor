/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

using namespace juce;
using namespace std;

//==============================================================================
namespace params
{
    enum Names
    {
        kThreshold,
        kAttack,
        kRelease,
        kRatio,
        kBypass,
        kMute,
        kSolo,
        
        kPluginBypass,
        kInputGain,
        kOutputGain,
        kDryWetMix
    };
    inline const map<Names, String>& GetParams()
    {
        static map<Names, String> params = {
            
            {kThreshold, "Threshold"},
            {kAttack, "Attack"},
            {kRelease, "Release"},
            {kRatio, "Ratio"},
            {kBypass, "Bypass"},
            {kMute, "Mute"},
            {kSolo, "Solo"},
            
            {kPluginBypass, "Plugin Bypass"},
            {kInputGain, "Input Gain"},
            {kOutputGain, "Output Gain"},
            {kDryWetMix, "Dry/Wet Mix"}
        };
        return params;
    }
}

//==============================================================================
/**
*/
struct Compressor
{
    AudioParameterFloat* threshold { nullptr };
    AudioParameterFloat* attack { nullptr };
    AudioParameterFloat* release { nullptr };
    AudioParameterFloat* ratio { nullptr };
    AudioParameterBool* bypass { nullptr };
    AudioParameterBool* mute { nullptr };
    AudioParameterBool* solo { nullptr };
    
    void prepare(const dsp::ProcessSpec& spec)
    {
        compressor.prepare(spec);
    }
    
    void updateCompressorSettings()
    {
        compressor.setThreshold(threshold->get());
        compressor.setAttack(attack->get());
        compressor.setRelease(release->get());
        compressor.setRatio(ratio->get());
    }
    
    void process(AudioBuffer<float>& buffer)
    {
        auto ab = dsp::AudioBlock<float>(buffer);
        auto pc = dsp::ProcessContextReplacing<float>(ab);
        pc.isBypassed = bypass->get();
        compressor.process(pc);
    }
private:
    dsp::Compressor<float> compressor;
};

//==============================================================================

class ParallelcompressorAudioProcessor  : public foleys::MagicProcessor
{
public:
    //==============================================================================
    ParallelcompressorAudioProcessor();
    ~ParallelcompressorAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    AudioProcessorValueTreeState apvts {*this, nullptr, "Params", createParameterLayout()};
    
private:
    //==============================================================================
    AudioParameterFloat* dry_wet_mix_param_ { nullptr };
    AudioParameterFloat* input_gain_param_ { nullptr };
    AudioParameterFloat* output_gain_param_ { nullptr };
    AudioParameterBool* bypass_param_ { nullptr };
    
    //==============================================================================
    AudioBuffer<float> dry_buffer_;
    AudioBuffer<float> wet_buffer_;
    dsp::Gain<float> input_gain_, output_gain_;
    Compressor comp;
    
    //==============================================================================
    template<typename B, typename G>
    void applyGain(B& buffer, G& gain) {
        auto ab = dsp::AudioBlock<float>(buffer);
        auto pc = dsp::ProcessContextReplacing<float>(ab);
        gain.process(pc);
    }
    
    //==============================================================================
    void updateState();
    
    //==============================================================================
    foleys::MagicLevelSource* input_meter  = nullptr;
    foleys::MagicLevelSource* output_meter  = nullptr;
    foleys::MagicPlotSource* input_analyzer = nullptr;
    foleys::MagicPlotSource* output_analyzer = nullptr;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParallelcompressorAudioProcessor)
};
