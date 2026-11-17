// Test-only compressor that publishes its gain reduction as a read-only meter
// parameter, the way many commercial dynamics plugins do. Used to check that
// LiveRack's GR meters work through real VST3 hosting.
#include <juce_audio_processors/juce_audio_processors.h>

class DemoCompressor : public juce::AudioProcessor
{
public:
    DemoCompressor()
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                           .withOutput ("Out", juce::AudioChannelSet::stereo()))
    {
        addParameter (threshold = new juce::AudioParameterFloat (juce::ParameterID { "threshold", 1 }, "Threshold",
                                                                 -50.0f, 0.0f, -24.0f));
        addParameter (ratio = new juce::AudioParameterFloat (juce::ParameterID { "ratio", 1 }, "Ratio",
                                                             1.0f, 20.0f, 6.0f));
        addParameter (gainReduction = new juce::AudioParameterFloat (juce::ParameterID { "gr", 1 }, "Gain Reduction",
            juce::NormalisableRange<float> (0.0f, 30.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withAutomatable (false)
                .withCategory (juce::AudioProcessorParameter::compressorLimiterGainReductionMeter)));
    }

    const juce::String getName() const override { return "DemoCompressor"; }
    void prepareToPlay (double sr, int) override { sampleRate = sr; envelope = 0.0f; setLatencySamples (juce::roundToInt (0.003 * sr)); } // reports a 3 ms lookahead (for latency-group tests)
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
        if (in.isDisabled()) return false;
        // Same in and out, or mono in -> stereo out (for mono-to-stereo tests)
        return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const float attack  = std::exp (-1.0f / (0.005f * (float) sampleRate));
        const float release = std::exp (-1.0f / (0.120f * (float) sampleRate));
        const float thr = threshold->get(), r = ratio->get();
        float maxGr = 0.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < getTotalNumInputChannels(); ++c)
                peak = juce::jmax (peak, std::abs (buffer.getSample (c, i)));

            const float coeff = peak > envelope ? attack : release;
            envelope = coeff * envelope + (1.0f - coeff) * peak;

            const float levelDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
            const float over = levelDb - thr;
            const float grDb = over > 0.0f ? over * (1.0f - 1.0f / r) : 0.0f;
            maxGr = juce::jmax (maxGr, grDb);

            const float gain = juce::Decibels::decibelsToGain (-grDb);
            for (int c = 0; c < getTotalNumInputChannels(); ++c)
                buffer.setSample (c, i, buffer.getSample (c, i) * gain);
        }

        gainReduction->setValueNotifyingHost (gainReduction->convertTo0to1 (juce::jmin (30.0f, maxGr)));

        // Mono in, stereo out: the right side copies the left
        if (getTotalNumInputChannels() == 1 && getTotalNumOutputChannels() == 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream out (data, false);
        out.writeFloat (threshold->get());
        out.writeFloat (ratio->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        *threshold = in.readFloat();
        *ratio = in.readFloat();
    }

private:
    juce::AudioParameterFloat* threshold = nullptr;
    juce::AudioParameterFloat* ratio = nullptr;
    juce::AudioParameterFloat* gainReduction = nullptr;
    double sampleRate = 48000.0;
    float envelope = 0.0f;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DemoCompressor(); }
