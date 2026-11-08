#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <condition_variable>
#include <mutex>
#include <queue>

// Out-of-process plugin scanning, adapted from JUCE's AudioPluginHost.
//
// LiveRack launches a second copy of itself as a hidden "scanner" process and
// asks it to load each plugin. If a plugin crashes or hangs, only the scanner
// dies: LiveRack kills it, skips that plugin (puts it on the blacklist, which
// the scan window lets you clear) and carries on with a fresh scanner.
namespace PluginScanning
{
    constexpr const char* workerUID = "liverackpluginscanner";
    constexpr int timeoutSeconds = 300; // WaveShell-style bundles can legitimately take minutes

    //==========================================================================
    // Runs inside the scanner process
    class Worker final : private juce::ChildProcessWorker,
                         private juce::AsyncUpdater
    {
    public:
        Worker() { formatManager.addDefaultFormats(); }

        using juce::ChildProcessWorker::initialiseFromCommandLine;

    private:
        void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
        {
            if (mb.isEmpty())
                return;

            const std::lock_guard<std::mutex> lock (mutex);

            if (const auto results = doScan (mb); ! results.isEmpty())
            {
                sendResults (results);
            }
            else
            {
                // Some formats (e.g. AudioUnits) must be loaded on the message thread
                pendingBlocks.emplace (mb);
                triggerAsyncUpdate();
            }
        }

        void handleConnectionLost() override
        {
            juce::JUCEApplicationBase::quit();
        }

        void handleAsyncUpdate() override
        {
            for (;;)
            {
                const std::lock_guard<std::mutex> lock (mutex);

                if (pendingBlocks.empty())
                    return;

                sendResults (doScan (pendingBlocks.front()));
                pendingBlocks.pop();
            }
        }

        juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock& block)
        {
            juce::MemoryInputStream stream { block, false };
            const auto formatName = stream.readString();
            const auto identifier = stream.readString();

            juce::PluginDescription pd;
            pd.fileOrIdentifier = identifier;
            pd.uniqueId = pd.deprecatedUid = 0;

            juce::AudioPluginFormat* matchingFormat = nullptr;
            for (auto* format : formatManager.getFormats())
                if (format->getName() == formatName)
                    matchingFormat = format;

            juce::OwnedArray<juce::PluginDescription> results;

            if (matchingFormat != nullptr
                && (juce::MessageManager::getInstance()->isThisTheMessageThread()
                    || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
            {
                matchingFormat->findAllTypesForFile (results, identifier);
            }

            return results;
        }

        void sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
        {
            juce::XmlElement xml ("LIST");

            for (const auto& desc : results)
                xml.addChildElement (desc->createXml().release());

            const auto str = xml.toString();
            sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
        }

        std::mutex mutex;
        std::queue<juce::MemoryBlock> pendingBlocks;
        juce::AudioPluginFormatManager formatManager;
    };

    //==========================================================================
    // Runs in LiveRack: owns one scanner process
    class Coordinator final : private juce::ChildProcessCoordinator
    {
    public:
        Coordinator()
        {
            launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), workerUID, 0, 0);
        }

        ~Coordinator() override { killWorkerProcess(); }

        enum class State { timeout, gotResult, connectionLost };

        struct Response
        {
            State state;
            std::unique_ptr<juce::XmlElement> xml;
        };

        Response getResponse()
        {
            std::unique_lock<std::mutex> lock { mutex };

            if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 }, [this] { return gotResult || connectionLost; }))
                return { State::timeout, nullptr };

            const auto state = connectionLost ? State::connectionLost : State::gotResult;
            connectionLost = false;
            gotResult = false;

            return { state, std::move (descriptions) };
        }

        using juce::ChildProcessCoordinator::sendMessageToWorker;

    private:
        void handleMessageFromWorker (const juce::MemoryBlock& mb) override
        {
            const std::lock_guard<std::mutex> lock { mutex };
            descriptions = juce::parseXML (mb.toString());
            gotResult = true;
            condvar.notify_one();
        }

        void handleConnectionLost() override
        {
            const std::lock_guard<std::mutex> lock { mutex };
            connectionLost = true;
            condvar.notify_one();
        }

        std::mutex mutex;
        std::condition_variable condvar;
        std::unique_ptr<juce::XmlElement> descriptions;
        bool connectionLost = false, gotResult = false;
    };

    //==========================================================================
    // Plugged into the KnownPluginList so every scan goes through the scanner process
    class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format,
                                 juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            if (coordinator == nullptr)
                coordinator = std::make_unique<Coordinator>();

            juce::MemoryBlock block;
            {
                juce::MemoryOutputStream stream { block, true };
                stream.writeString (format.getName());
                stream.writeString (fileOrIdentifier);
            }

            if (! coordinator->sendMessageToWorker (block))
            {
                coordinator.reset();
                return false;
            }

            const auto started = juce::Time::getMillisecondCounter();

            for (;;)
            {
                if (shouldExit())
                    return true;

                auto response = coordinator->getResponse();

                if (response.state == Coordinator::State::timeout)
                {
                    // Plugin is hanging: kill the scanner and skip this plugin
                    if (juce::Time::getMillisecondCounter() - started > (juce::uint32) timeoutSeconds * 1000)
                    {
                        coordinator.reset();
                        return false;
                    }

                    continue;
                }

                if (response.xml != nullptr)
                {
                    for (const auto* item : response.xml->getChildIterator())
                    {
                        auto desc = std::make_unique<juce::PluginDescription>();
                        if (desc->loadFromXml (*item))
                            result.add (std::move (desc));
                    }
                }

                if (response.state != Coordinator::State::gotResult)
                {
                    coordinator.reset(); // scanner crashed on this plugin
                    return false;
                }

                return true;
            }
        }

        void scanFinished() override { coordinator.reset(); }

    private:
        std::unique_ptr<Coordinator> coordinator;
    };
}
