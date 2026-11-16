#include "UI/ExpressionPedal.h"

namespace openguitarmultifx
{

class ExpressionPedalTests : public juce::UnitTest
{
public:
    ExpressionPedalTests() : juce::UnitTest ("ExpressionPedal", "Presets") {}

    void runTest() override
    {
        using namespace exppedal;

        beginTest ("an uncalibrated linear pedal maps 0-127 straight to 0-1");
        {
            Pedal p;
            expectWithinAbsoluteError (p.shape (0), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.shape (127), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.shape (64), 64.0f / 127.0f, 1.0e-6f);
        }

        beginTest ("calibration stretches the pedal's real travel to the full range and clamps past it");
        {
            Pedal p;
            p.heel = 10;
            p.toe = 110;
            expectWithinAbsoluteError (p.shape (10), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.shape (110), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.shape (60), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (p.shape (0), 0.0f, 1.0e-6f, "below the heel stays at 0");
            expectWithinAbsoluteError (p.shape (127), 1.0f, 1.0e-6f, "past the toe stays at 1");
        }

        beginTest ("invert flips the direction; curves keep the ends and bend the middle");
        {
            Pedal p;
            p.invert = true;
            expectWithinAbsoluteError (p.shape (0), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.shape (127), 0.0f, 1.0e-6f);

            Pedal logP;
            logP.curve = Curve::log;
            Pedal antiP;
            antiP.curve = Curve::antiLog;
            for (auto* q : { &logP, &antiP })
            {
                expectWithinAbsoluteError (q->shape (0), 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (q->shape (127), 1.0f, 1.0e-6f);
            }
            const float mid = Pedal{}.shape (64);
            expect (logP.shape (64) < mid, "log is slow at the heel");
            expect (antiP.shape (64) > mid, "anti-log is fast at the heel");
        }

        beginTest ("calibrator takes the range the pedal was rocked through, refuses a pedal that did not move");
        {
            Calibrator c;
            c.begin();
            for (int v : { 40, 3, 70, 121, 90 })
                c.observe (v);
            Pedal p;
            expect (c.finish (p));
            expectEquals (p.heel, 3);
            expectEquals (p.toe, 121);

            Calibrator still;
            still.begin();
            still.observe (50);
            still.observe (52);
            Pedal untouched;
            expect (! still.finish (untouched));
            expectEquals (untouched.heel, 0);
            expectEquals (untouched.toe, 127);
        }

        beginTest ("auto-engage: moving switches on once, resting at the heel switches off after the delay");
        {
            using A = AutoEngage::Action;
            AutoEngage a;
            expect (a.onValue (0.0f, 0) == A::none, "pedal at rest from the start does nothing");
            expect (a.tick (1000, 500) == A::none);
            expect (a.onValue (0.4f, 1100) == A::engage);
            expect (a.onValue (0.8f, 1150) == A::none, "already on");
            expect (a.onValue (0.0f, 1200) == A::none);
            expect (a.tick (1600, 500) == A::none, "not rested long enough yet");
            expect (a.onValue (0.3f, 1650) == A::none, "moving again before the delay keeps it on");
            expect (a.onValue (0.0f, 1700) == A::none);
            expect (a.tick (2199, 500) == A::none);
            expect (a.tick (2200, 500) == A::disengage);
            expect (a.tick (5000, 500) == A::none, "only once");
            expect (a.onValue (0.5f, 5100) == A::engage, "and on again with the next move");
        }

        beginTest ("one CC drives one pedal");
        {
            Settings s;
            s.assignCc (0, 11);
            expectEquals (s.indexForCc (11), 0);
            s.assignCc (1, 11);
            expectEquals (s.indexForCc (11), 1);
            expectEquals (s.pedals[0].cc, -1);
            expectEquals (s.indexForCc (12), -1);
        }

        beginTest ("settings round-trip through XML; bad values fall back instead of failing");
        {
            Settings s;
            s.assignCc (0, 11);
            s.pedals[0].heel = 5;
            s.pedals[0].toe = 120;
            s.pedals[0].curve = Curve::log;
            s.pedals[0].autoEngage = true;
            s.pedals[0].autoOffMs = 800;
            s.assignCc (1, 7);
            s.pedals[1].invert = true;

            const auto back = fromXml (toXml (s).get());
            expect (back.pedals[0] == s.pedals[0]);
            expect (back.pedals[1] == s.pedals[1]);

            juce::XmlElement bad ("ExpressionPedals");
            auto* e = bad.createNewChildElement ("Pedal");
            e->setAttribute ("index", 0);
            e->setAttribute ("cc", 69);
            e->setAttribute ("heel", 60);
            e->setAttribute ("toe", 62);
            e->setAttribute ("curve", 9);
            e->setAttribute ("autoOffMs", 1);
            bad.createNewChildElement ("Pedal")->setAttribute ("index", 7);
            const auto fixed = fromXml (&bad);
            expectEquals (fixed.pedals[0].cc, -1, "the snapshot-select CC is refused");
            expectEquals (fixed.pedals[0].heel, 0);
            expectEquals (fixed.pedals[0].toe, 127);
            expect (fixed.pedals[0].curve == Curve::linear);
            expectEquals (fixed.pedals[0].autoOffMs, minAutoOffMs);

            expect (fromXml (nullptr).pedals[0] == Pedal{}, "no file = defaults");
        }
    }
};

static ExpressionPedalTests expressionPedalTests;

} // namespace openguitarmultifx
