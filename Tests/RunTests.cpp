#include <juce_core/juce_core.h>

// Each test file registers its juce::UnitTest classes through a static
// global instance (the pattern JUCE's own test framework expects). This
// main() just needs to trigger the runner.
int main (int argc, char* argv[])
{
    juce::UnitTestRunner runner;
    runner.setPassesAreLogged (false);

    // Optional first argument: only run the suites whose name contains it (the full suite takes minutes).
    if (argc > 1)
    {
        juce::Array<juce::UnitTest*> selected;
        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getName().containsIgnoreCase (argv[1]))
                selected.add (test);
        runner.runTests (selected);
    }
    else
    {
        runner.runAllTests();
    }

    int numFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        numFailures += runner.getResult (i)->failures;

    if (numFailures > 0)
    {
        juce::Logger::writeToLog (juce::String (numFailures) + " test failure(s).");
        return 1;
    }

    juce::Logger::writeToLog ("All tests passed.");
    return 0;
}
