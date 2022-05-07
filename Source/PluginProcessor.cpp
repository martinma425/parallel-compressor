/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ParallelcompressorAudioProcessor::ParallelcompressorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : MagicProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    // Autosave Option
    // FOLEYS_SET_SOURCE_PATH (__FILE__);
    
    auto file = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getChildFile ("Contents")
        .getChildFile ("Resources")
        .getChildFile ("magic.xml");

    if (file.existsAsFile())
        magicState.setGuiValueTree (file);
    else
        magicState.setGuiValueTree (BinaryData::magic_xml, BinaryData::magic_xmlSize);
    
    using namespace params;
    const auto& params = GetParams();
    
    auto SetFloatParam = [&apvts = this->apvts, &params](auto& param, const auto& param_name)
    {
        param = dynamic_cast<AudioParameterFloat*>(apvts.getParameter(params.at(param_name)));
        jassert(param != nullptr);
    };
    
    auto SetBoolParam = [&apvts = this->apvts, &params](auto& param, const auto& param_name)
    {
        param = dynamic_cast<AudioParameterBool*>(apvts.getParameter(params.at(param_name)));
        jassert(param != nullptr);
    };
    
    SetFloatParam(comp.threshold, Names::kThreshold);
    SetFloatParam(comp.attack, Names::kAttack);
    SetFloatParam(comp.release, Names::kRelease);
    SetFloatParam(comp.ratio, Names::kRatio);
    SetBoolParam(comp.bypass, Names::kBypass);
    SetBoolParam(comp.mute, Names::kMute);
    SetBoolParam(comp.solo, Names::kSolo);
    
    SetBoolParam(bypass_param_, Names::kPluginBypass);
    SetFloatParam(input_gain_param_, Names::kInputGain);
    SetFloatParam(output_gain_param_, Names::kOutputGain);
    SetFloatParam(dry_wet_mix_param_, Names::kDryWetMix);
    
    output_meter  = magicState.createAndAddObject<foleys::MagicLevelSource>("output_meter");
    input_analyzer = magicState.createAndAddObject<foleys::MagicAnalyser>("input_analyzer");
    output_analyzer = magicState.createAndAddObject<foleys::MagicAnalyser>("output_analyzer");
}

ParallelcompressorAudioProcessor::~ParallelcompressorAudioProcessor()
{
}

//==============================================================================
const juce::String ParallelcompressorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ParallelcompressorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ParallelcompressorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ParallelcompressorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ParallelcompressorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ParallelcompressorAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ParallelcompressorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ParallelcompressorAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ParallelcompressorAudioProcessor::getProgramName (int index)
{
    return {};
}

void ParallelcompressorAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void ParallelcompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    dsp::ProcessSpec spec;
        
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    spec.sampleRate = sampleRate;
    
    comp.prepare(spec);
    input_gain_.prepare(spec);
    output_gain_.prepare(spec);
    
    input_gain_.setRampDurationSeconds(0.05);
    output_gain_.setRampDurationSeconds(0.05);
    
    dry_buffer_.setSize(spec.numChannels, spec.maximumBlockSize);
    wet_buffer_.setSize(spec.numChannels, spec.maximumBlockSize);

    output_meter->setupSource (getTotalNumOutputChannels(), sampleRate, 300);
    input_analyzer->prepareToPlay(sampleRate, samplesPerBlock);
    output_analyzer->prepareToPlay(sampleRate, samplesPerBlock);
}

void ParallelcompressorAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ParallelcompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void ParallelcompressorAudioProcessor::updateState()
{
    comp.updateCompressorSettings();
    input_gain_.setGainDecibels(input_gain_param_->get());
    output_gain_.setGainDecibels(output_gain_param_->get());
}

void ParallelcompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateState();
    applyGain(buffer, input_gain_);
    input_analyzer->pushSamples(buffer);
    
    // Copy over buffer data
    dry_buffer_ = buffer;
    wet_buffer_ = buffer;
        
    comp.process(wet_buffer_);
    auto num_samples = buffer.getNumSamples();
    auto num_channels = buffer.getNumChannels();
    
    auto SumFilterBuffers = [nc = num_channels, ns = num_samples](auto& input_buffer, const auto& src) {
        for (auto i = 0; i < nc; ++i) {
            input_buffer.addFrom(i, 0, src, i, 0, ns);
        }
    };
    
    float wet_mix = dry_wet_mix_param_->get();
    float dry_mix = 1 - wet_mix;
    wet_buffer_.applyGain(wet_mix);
    dry_buffer_.applyGain(dry_mix);
    
    if (!bypass_param_->get()) {
        buffer.clear();
        if (comp.solo->get()) {
            SumFilterBuffers(buffer, wet_buffer_);
        } else if (comp.mute->get()) {
            SumFilterBuffers(buffer, dry_buffer_);
        } else {
            SumFilterBuffers(buffer, wet_buffer_);
            SumFilterBuffers(buffer, dry_buffer_);
        }
    }
    
//    if (comp.solo->get()) {
//        buffer.clear();
//        SumFilterBuffers(buffer, wet_buffer_);
//    } else if (comp.mute->get()) {
//        buffer.clear();
//        SumFilterBuffers(buffer, dry_buffer_);
//    } else if (!bypass_param_->get()){
//        buffer.clear();
//        SumFilterBuffers(buffer, wet_buffer_);
//        SumFilterBuffers(buffer, dry_buffer_);
//    }
    
    applyGain(buffer, output_gain_);
    output_analyzer->pushSamples(buffer);
    output_meter->pushSamples(buffer);
}


//==============================================================================
AudioProcessorValueTreeState::ParameterLayout ParallelcompressorAudioProcessor::createParameterLayout()
{
    using namespace params;
    const auto& params = GetParams();
    AudioProcessorValueTreeState::ParameterLayout layout;
    
    auto threshold_db_range = NormalisableRange<float>(-60, 12, 0.1, 1);
    auto ar_time_range = NormalisableRange<float>(1, 500, 0.1, 1);
    auto ratio_range = NormalisableRange<float>(1, 100, 0.1, 0.5);
    auto gain_range = NormalisableRange<float>(-24, 24, 0.5, 1);
    auto dry_wet_range = Range<float>(0, 1);
    
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kThreshold),
                                                params.at(Names::kThreshold),
                                                threshold_db_range,
                                                0));
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kAttack),
                                                params.at(Names::kAttack),
                                                ar_time_range,
                                                50));
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kRelease),
                                                params.at(Names::kRelease),
                                                ar_time_range,
                                                250));
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kRatio),
                                                params.at(Names::kRatio),
                                                ratio_range,
                                                20));
    layout.add(make_unique<AudioParameterBool>(params.at(Names::kBypass),
                                               params.at(Names::kBypass),
                                               false));
    layout.add(make_unique<AudioParameterBool>(params.at(Names::kSolo),
                                               params.at(Names::kSolo),
                                               false));
    layout.add(make_unique<AudioParameterBool>(params.at(Names::kMute),
                                               params.at(Names::kMute),
                                               false));
    layout.add(make_unique<AudioParameterBool>(params.at(Names::kPluginBypass),
                                               params.at(Names::kPluginBypass),
                                               false));
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kInputGain),
                                                params.at(Names::kInputGain),
                                                gain_range,
                                                0));
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kOutputGain),
                                                params.at(Names::kOutputGain),
                                                gain_range,
                                                0));
    layout.add(make_unique<AudioParameterFloat>(params.at(Names::kDryWetMix),
                                                params.at(Names::kDryWetMix),
                                                dry_wet_range,
                                                0.5));
    
    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParallelcompressorAudioProcessor();
}
