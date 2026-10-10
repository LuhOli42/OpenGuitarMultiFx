#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace openguitarmultifx::midilearnxml
{

/** CC number -> the parameter it drives. Same shape as MainComponent::midiCcBindings. */
using Bindings = std::map<int, juce::AudioParameterFloat*>;

/** One parameter group per <Block> element of the preset, in the same order they appear in the XML (nullptr for a
    block that was skipped, e.g. an unknown key on load) -- a binding is stored as "block index + paramID" because the
    AudioParameterFloat* it points at today is destroyed and recreated on every preset load. */
using BlockGroups = std::vector<juce::AudioProcessorParameterGroup*>;

/** Appends a <MidiLearn> element holding one <Binding cc block param/> per binding whose parameter belongs to one of
    `blocks`. Nothing is written when no binding survives, so a preset without MIDI Learn looks exactly as it did. */
inline void write (juce::XmlElement& presetXml, const Bindings& bindings, const BlockGroups& blocks)
{
    juce::XmlElement* midiXml = nullptr;

    for (auto& [cc, param] : bindings)
        for (int block = 0; block < (int) blocks.size(); ++block)
        {
            auto* group = blocks[(size_t) block];
            if (group == nullptr || ! group->getParameters (true).contains (param))
                continue;

            if (midiXml == nullptr)
                midiXml = presetXml.createNewChildElement ("MidiLearn");

            auto* bindingXml = midiXml->createNewChildElement ("Binding");
            bindingXml->setAttribute ("cc", cc);
            bindingXml->setAttribute ("block", block);
            bindingXml->setAttribute ("param", param->paramID);
            break;
        }
}

/** The bindings stored in `presetXml`, resolved against the freshly-created `blocks`. A preset saved before MIDI Learn
    was persisted has no <MidiLearn> element and yields no bindings. An entry pointing at a block/parameter that no longer
    exists (a preset from another build, a renamed paramID) is skipped rather than failing the whole load. */
inline Bindings read (const juce::XmlElement& presetXml, const BlockGroups& blocks)
{
    Bindings bindings;

    auto* midiXml = presetXml.getChildByName ("MidiLearn");
    if (midiXml == nullptr)
        return bindings;

    for (auto* bindingXml : midiXml->getChildWithTagNameIterator ("Binding"))
    {
        const int cc = bindingXml->getIntAttribute ("cc", -1);
        const int block = bindingXml->getIntAttribute ("block", -1);
        const auto paramId = bindingXml->getStringAttribute ("param");

        if (cc < 0 || cc > 127 || block < 0 || block >= (int) blocks.size() || blocks[(size_t) block] == nullptr)
            continue;

        for (auto* p : blocks[(size_t) block]->getParameters (true))
            if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (floatParam->paramID == paramId)
                {
                    bindings[cc] = floatParam;
                    break;
                }
    }

    return bindings;
}

} // namespace openguitarmultifx::midilearnxml
