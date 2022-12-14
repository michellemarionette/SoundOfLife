#include "Headers.h"


//================================================//
// Synthesis class.

/**
    Constructor of the synthesis class.
    @param Grid Reference to a grid object.
 */

Synthesis::Synthesis (Grid& grid)
    :   m_Grid (grid)
{
    // Init oscillators.
    m_Oscillators.ensureStorageAllocated (Variables::numOscillators);
    m_Oscillators.ensureStorageAllocated (Variables::numLFOs);
    
    for (int i = 0; i < Variables::numOscillators; ++i)
        m_Oscillators.add (new SineOscillator());
    
    for (int i = 0; i < Variables::numLFOs; ++i)
        m_LFOs.add (new SineOscillator);
}

Synthesis::~Synthesis() {}


//================================================//
// Setter methods.

void Synthesis::setBlockSize (int blockSize)                        { m_BlockSize = blockSize; }
void Synthesis::setSampleRate (float sampleRate)                    { m_SampleRate = sampleRate; }


//================================================//
// Getter methods.

int Synthesis::getBlockSize()                                       { return m_BlockSize; }
float Synthesis::getSampleRate()                                    { return m_SampleRate; }


//================================================//
// Helper methods.

/**
    Returns a float representing a gain value for a given oscillator.
    @param oscillatorIndex Index of an oscillator.
 */


float Synthesis::getOscillatorGain (int oscillatorIndex)
{
    float gain = 0;
    
    float startColumn = oscillatorIndex * (Variables::numColumns / Variables::numOscillators);
    float endColumn = startColumn + (Variables::numColumns / Variables::numOscillators);
    
    //Sum all fade values in a block of cells.
    for (int column = startColumn; column < endColumn; ++column)
    {
        for (int row = 0; row < Variables::numRows; ++row)
            gain += m_Grid.getCell (row, column)->getFade();
    }
    
    // Normalize value to range [0,1].
    gain /= (float)Variables::numRows * (float)Variables::numColumns / (float)Variables::numOscillators;
    
    return gain;
}

/**
    Returns a float representing the pan value for a given oscillator.
    @param oscillatorIndex Index of an oscillator.
 */

float Synthesis::getOscillatorPan (int oscillatorIndex)
{
    float pan = 0;
    
    float startColumn = oscillatorIndex * (Variables::numColumns / Variables::numOscillators);
    float endColumn = startColumn + (Variables::numColumns / Variables::numOscillators);
    
    for (int column = startColumn; column < endColumn; ++column)
    {
        for (int row = 0; row < Variables::numRows; ++row)
        {
            Cell& cell = *m_Grid.getCell (row, column);
            
            if (row < Variables::numRows / 2)
                pan += cell.getFade();
            
            else
                pan -= cell.getFade();
        }
    }
    
    pan /= Variables::numRows * (endColumn - startColumn) / 2.0;
    
    if (pan > 1.0)
        pan = 1.0;
    else if (pan < -1.0)
        pan = -1.0;
    
    return pan;
}

/**
    Returns a float representing a gain value normalised based on frequency.
    @param gain Gain to be normalised.
    @param frequency Frequency used for normalisation.
 */

float Synthesis::getSpectralGainDecay (float gain, float frequency)
{
    // Explanation for this is here: https://en.wikipedia.org/wiki/Pink_noise
    return gain * Variables::startFrequency * (1.0f / frequency) ;
}


//================================================//
// State methods.

/**
    Updates fade values of a block of cells given an oscillator.
    @param oscillatorIndex Index of an oscillator.
 */

void Synthesis::updateFadeValues (int oscillatorIndex)
{
    float startColumn = oscillatorIndex * (Variables::numColumns / Variables::numOscillators);
    float endColumn = startColumn + (Variables::numColumns / Variables::numOscillators);
    
    for (int column = startColumn; column < endColumn; ++column)
        for (int row = 0; row < Variables::numRows; ++row)
            m_Grid.getCell (row, column)->updateFade();
}


//================================================//
// Init methods.

/**
    Prepares all components to be processe.
    @param sampleRate Sample rate to be used.
    @param blockSize Block size to be used.
 */

void Synthesis::prepareToPlay (float sampleRate, int blockSize)
{
    // Setup oscillators.
    auto frequency = Variables::startFrequency;
    
    for (int i = 0; i < Variables::numOscillators; ++i)
    {
        m_Oscillators[i]->prepareToPlay (frequency, sampleRate, blockSize);
        
        // Harmonic series: https://en.wikipedia.org/wiki/Harmonic_series_(mathematics)
        // And also this: https://en.wikipedia.org/wiki/Inharmonicity
        frequency += frequency / (i + 1.0f) * Variables::inharmonicity;
    }
    
    // Setup LFOs.
    for (int i = 0; i < Variables::numLFOs; ++i)
        m_LFOs[i]->prepareToPlay (Variables::frequencyLFO[i], sampleRate, blockSize);

    m_FilterModulator.prepareToPlay(0.1f, sampleRate, blockSize);
    
    // Set member variables.
    setBlockSize (blockSize);
    setSampleRate (sampleRate);
    
    // Setup filter.
    m_FilterLeft.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, Variables::filterCutoff));
    m_FilterRight.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, Variables::filterCutoff));
    
    // Setup reverb.
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.dryLevel = 0.5f;
    reverbParameters.wetLevel = 0.5f;
    reverbParameters.roomSize = 1.0f;
    m_Reverb.reset();
}


//================================================//
// DSP methods.

/**
    Processes all audio content and inserts into an audio buffer.
    @param buffer Reference to an audio buffer.
 */

void Synthesis::processBlock (juce::AudioBuffer<float>& buffer)
{
    int numChannels = buffer.getNumChannels();
    int blockSize = buffer.getNumSamples();
    
    float sample = 0;
    float gain;
    
    juce::AudioBuffer<float> block;
    juce::Array<float> panValues;
    
    block.setSize (numChannels, blockSize);
    panValues.ensureStorageAllocated (m_BlockSize);
    
    buffer.clear();
    block.clear();
    
    for (int oscillatorIndex = 0; oscillatorIndex < Variables::numOscillators; ++oscillatorIndex)
    {
        panValues.clearQuick();
        
        for (int i = 0; i < blockSize; ++i)
        {
            auto modulator = m_LFOs[oscillatorIndex % Variables::numLFOs]->processSample();
            
            // Frequency modulation.
            auto currentFrequency = m_Oscillators[oscillatorIndex]->getFrequency();
            auto modulatedFrequency = currentFrequency + ((currentFrequency / ((oscillatorIndex + 1) * 5)) * modulator);
            
            m_Oscillators[oscillatorIndex]->setFrequency (modulatedFrequency);
            m_Oscillators[oscillatorIndex]->updatePhaseDelta();
            
            // Sample to be further processed.
            sample = m_Oscillators[oscillatorIndex]->processSample();
            
            // Gain to be applied.
            gain = getOscillatorGain (oscillatorIndex);
            gain *= getSpectralGainDecay (gain, m_Oscillators[oscillatorIndex]->getFrequency());
            
            // Pan to be applied.
            panValues.add (getOscillatorPan (oscillatorIndex));
            
            // Update fade values for entire column.
            updateFadeValues (oscillatorIndex);
        
            // Apply processing to sample.
            sample *= gain;
            
            // Add to buffer.
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getWritePointer (channel);
                channelData[i] += sample;
            }
            
            // Reset values.
            m_Oscillators[oscillatorIndex]->setFrequency (currentFrequency);
        }
        
        // Apply pan to buffer.
        m_Panner.processBlock (block, panValues);
    }
    
    // Add to final audio buffer.
    for (int channel = 0; channel < numChannels; ++channel)
        buffer.addFrom (channel, 0, block, channel, 0, blockSize);
    
    
    auto* leftChannel = buffer.getWritePointer (0);
    auto* rightChannel = buffer.getWritePointer (1);
    
    // Apply filter.
    auto filterModulator = m_FilterModulator.processSample();
    m_FilterLeft.setCoefficients (juce::IIRCoefficients::makeLowPass (m_SampleRate, Variables::filterCutoff * (filterModulator + 1.001f) * 100.0f));
    m_FilterRight.setCoefficients (juce::IIRCoefficients::makeLowPass (m_SampleRate, Variables::filterCutoff * (filterModulator + 1.001f) * 100.0f));
    
    m_FilterLeft.processSamples (leftChannel, buffer.getNumSamples());
    m_FilterRight.processSamples (rightChannel, buffer.getNumSamples());
    
    
    // Apply distortion.
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        leftChannel[i] = std::tanhf (leftChannel[i] * 5.0f);
        rightChannel[i] = std::tanhf (rightChannel[i] * 5.0f);
    }
    
    // Apply reverb.
    m_Reverb.processStereo (leftChannel, rightChannel, buffer.getNumSamples());
    m_Reverb.reset();
}
