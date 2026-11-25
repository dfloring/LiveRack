// Test-only plugin with an optional side chain input (the second input bus, off until the host
// asks for it, and mono or stereo). Its output is the main input plus the side chain, so what
// reached the bus shows in the output. Used to check LiveRack's side chain routing.
#include <juce_audio_processors/juce_audio_processors.h>

class SidechainProbe : public juce::AudioProcessor
{
public:
    SidechainProbe()
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                           .withOutput ("Out", juce::AudioChannelSet::stereo())
                                           .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)) {}

    const juce::String getName() const override { return "SidechainProbe"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet(), sc = l.getChannelSet (true, 1);
        return ! in.isDisabled() && in == out
               && (sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int main = getMainBusNumInputChannels(), sc = getTotalNumInputChannels() - main;

        if (sc > 0)
            for (int c = 0; c < main; ++c)
                buffer.addFrom (c, 0, buffer, main + juce::jmin (c, sc - 1), 0, buffer.getNumSamples());
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
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SidechainProbe(); }
