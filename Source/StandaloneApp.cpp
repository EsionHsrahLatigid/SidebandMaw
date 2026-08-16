#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
class SidebandMawStandaloneApplication final : public juce::JUCEApplication
{
public:
    SidebandMawStandaloneApplication()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        applicationProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder>(
            applicationProperties.getUserSettings(), false);
        holder->getMuteInputValue().setValue(false);

        mainWindow = std::make_unique<juce::StandaloneFilterWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            std::move(holder));
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        applicationProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            if (auto* holder = mainWindow->getPluginHolder())
                holder->savePluginState();

        quit();
    }

private:
    juce::ApplicationProperties applicationProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
} // namespace

START_JUCE_APPLICATION(SidebandMawStandaloneApplication)
