#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

namespace openguitarmultifx
{

/** Numerically safe ln(1 + e^x) and its derivative, the logistic function. */
struct SoftPlus
{
    double value;
    double slope;

    static SoftPlus of (double x) noexcept
    {
        if (x > 30.0)
            return { x, 1.0 };
        if (x < -30.0)
        {
            const double e = std::exp (x);
            return { e, e };
        }
        const double e = std::exp (x);
        return { std::log1p (e), e / (1.0 + e) };
    }
};

/**
    How many volts wide the smooth floor at 0 V (see `floorPlateVoltage()`) is. Chosen so a plate more than ~30
    of these volts positive is untouched to the bit (SoftPlus's own x>30 fast path returns its input unchanged,
    slope exactly 1.0) -- normal tube operation (tens to hundreds of volts of plate swing) never comes close, so
    this only ever does anything within a few volts of a plate crossing its own cathode.
*/
constexpr double plateVoltageFloorScale = 2.0;

/**
    Smooth floor at 0 V for a plate/anode voltage, replacing "vpk<=0 -> exactly zero current AND zero gradient"
    with a real (if tiny) restoring gradient: the same asymmetric dead-zone pattern already fixed on the grid
    side (see KorenTriode::evaluate()'s and GridCurrent::evaluate()'s own comments), on the plate side. A Newton
    iterate can transiently push a plate below its own cathode while hunting for the solution even though the
    physical device never settles there -- found tracing the Bassman's phase-inverter/power-pentode stage under
    extreme sustained drive to a near-singular local Jacobian at exactly a plate-voltage port (docs/circuits/
    Bassman5F6A.md). SoftPlus's exp() tail keeps `vf` smooth and strictly positive arbitrarily far into the
    negative side, so `evaluate()`'s downstream formula never needs its own vpk<=0 special case at all.
*/
inline void floorPlateVoltage (double& vpk, double& dVpkFloor) noexcept
{
    const auto sp = SoftPlus::of (vpk / plateVoltageFloorScale);
    vpk = plateVoltageFloorScale * sp.value;
    dVpkFloor = sp.slope;
}


/**
    A function tabulated with its derivative and evaluated by cubic Hermite interpolation: value and slope in ~20
    cycles, C1-continuous (Newton sees a smooth model), against ~100 ns for a pow/exp/log chain. Built once, on the
    control thread; the audio thread only reads it.
*/
class HermiteTable
{
public:
    template <typename Fn>
    void build (double xa, double xb, int count, Fn&& fn)
    {
        x0 = xa;
        n = count;
        h = (xb - xa) / (double) (count - 1);
        invH = 1.0 / h;
        nodes.resize ((size_t) count);
        for (int i = 0; i < count; ++i)
        {
            double v, dv;
            fn (xa + h * (double) i, v, dv);
            nodes[(size_t) i] = { v, dv * h }; // slope stored per node interval (scaled by h)
        }
    }

    double lowEdge() const noexcept { return x0; }
    double highEdge() const noexcept { return x0 + h * (double) (n - 1); }

    /** `x` must be inside [lowEdge(), highEdge()]. */
    void evaluate (double x, double& y, double& dy) const noexcept
    {
        const double t = (x - x0) * invH;
        int i = (int) t;
        i = i < 0 ? 0 : (i > n - 2 ? n - 2 : i);
        const double u = t - (double) i;
        const double u2 = u * u, u3 = u2 * u;
        const Node* p = nodes.data() + i;
        const double f0 = p[0].f, m0 = p[0].m, f1 = p[1].f, m1 = p[1].m;
        y = (2.0 * u3 - 3.0 * u2 + 1.0) * f0 + (u3 - 2.0 * u2 + u) * m0 + (-2.0 * u3 + 3.0 * u2) * f1 + (u3 - u2) * m1;
        dy = ((6.0 * u2 - 6.0 * u) * (f0 - f1) + (3.0 * u2 - 4.0 * u + 1.0) * m0 + (3.0 * u2 - 2.0 * u) * m1) * invH;
    }

    /** Value only (one fewer polynomial). */
    double value (double x) const noexcept
    {
        const double t = (x - x0) * invH;
        int i = (int) t;
        i = i < 0 ? 0 : (i > n - 2 ? n - 2 : i);
        const double u = t - (double) i;
        const double u2 = u * u, u3 = u2 * u;
        const Node* p = nodes.data() + i;
        return (2.0 * u3 - 3.0 * u2 + 1.0) * p[0].f + (u3 - 2.0 * u2 + u) * p[0].m + (-2.0 * u3 + 3.0 * u2) * p[1].f + (u3 - u2) * p[1].m;
    }

private:
    struct Node { double f, m; };
    double x0 = 0.0, h = 1.0, invH = 1.0;
    int n = 2;
    std::vector<Node> nodes;
};

/** Tables shared by every tube with the same exponents / knee constants (built lazily, read-only afterwards). */
struct TubeTables
{
    /** softplus(a)^ex over a in [-40, 100]: the "E1^ex" of a Koren tube divided by its (vpk/kp)^ex or (vg2/kp)^ex factor. */
    static std::shared_ptr<const HermiteTable> softplusPower (double ex)
    {
        static std::mutex lock;
        static std::map<double, std::shared_ptr<const HermiteTable>> cache;
        const std::lock_guard<std::mutex> guard (lock);
        auto& slot = cache[ex];
        if (slot == nullptr)
        {
            auto t = std::make_shared<HermiteTable>();
            t->build (-40.0, 100.0, 2801, [ex] (double a, double& y, double& dy)
            {
                const auto sp = SoftPlus::of (a);
                const double base = std::max (sp.value, 1.0e-300);
                y = std::pow (base, ex);
                dy = ex * std::pow (base, ex - 1.0) * sp.slope;
            });
            slot = std::move (t);
        }
        return slot;
    }

    /** (v / k)^ex over v in [0, 700] (a triode's plate factor). */
    static std::shared_ptr<const HermiteTable> plateFactor (double k, double ex)
    {
        static std::mutex lock;
        static std::map<std::pair<double, double>, std::shared_ptr<const HermiteTable>> cache;
        const std::lock_guard<std::mutex> guard (lock);
        auto& slot = cache[{ k, ex }];
        if (slot == nullptr)
        {
            auto t = std::make_shared<HermiteTable>();
            t->build (0.0, 700.0, 1401, [k, ex] (double v, double& y, double& dy)
            {
                const double r = std::max (v / k, 1.0e-300);
                y = std::pow (r, ex);
                dy = ex * std::pow (r, ex - 1.0) / k;
            });
            slot = std::move (t);
        }
        return slot;
    }

    /** Dempwolf grid current as a function of x = Cg * vgk over [-30, 60]: i = Gg (softplus(x)/Cg)^xi, di/dx. */
    static std::shared_ptr<const HermiteTable> gridCurrent (double gg, double cg, double xi)
    {
        static std::mutex lock;
        static std::map<std::tuple<double, double, double>, std::shared_ptr<const HermiteTable>> cache;
        const std::lock_guard<std::mutex> guard (lock);
        auto& slot = cache[{ gg, cg, xi }];
        if (slot == nullptr)
        {
            auto t = std::make_shared<HermiteTable>();
            t->build (-30.0, 60.0, 1801, [gg, cg, xi] (double x, double& y, double& dy)
            {
                const auto sp = SoftPlus::of (x);
                const double base = std::max (sp.value / cg, 1.0e-300);
                y = gg * std::pow (base, xi);
                dy = gg * xi * std::pow (base, xi - 1.0) * sp.slope / cg;
            });
            slot = std::move (t);
        }
        return slot;
    }

    /** atan(v / k) over v in [-100, 800] (a pentode's knee). */
    static std::shared_ptr<const HermiteTable> knee (double k)
    {
        static std::mutex lock;
        static std::map<double, std::shared_ptr<const HermiteTable>> cache;
        const std::lock_guard<std::mutex> guard (lock);
        auto& slot = cache[k];
        if (slot == nullptr)
        {
            auto t = std::make_shared<HermiteTable>();
            t->build (-100.0, 800.0, 3601, [k] (double v, double& y, double& dy)
            {
                y = std::atan (v / k);
                dy = (1.0 / k) / (1.0 + (v / k) * (v / k));
            });
            slot = std::move (t);
        }
        return slot;
    }
};

/**
    Grid current of a receiving tube: Dempwolf's smooth form i = Gg * (ln(1 + e^(Cg v)) / Cg)^xi -- exactly zero-ish
    for a negative grid, a power law once the grid is driven positive (where the grid becomes a diode to the cathode).
*/
struct GridCurrent
{
    double Gg = 6.177e-4, Cg = 9.901, xi = 1.314;
    std::shared_ptr<const HermiteTable> table; // filled by the tube's setParameters()

    void prepareTable() { table = TubeTables::gridCurrent (Gg, Cg, xi); }

    void evaluate (double vgk, double& i, double& di) const noexcept
    {
        const double x = Cg * vgk;
        if (table != nullptr && x > table->lowEdge() && x < table->highEdge() - 1.0)
        {
            double dx;
            table->evaluate (x, i, dx);
            di = dx * Cg;
            return;
        }
        // No hard floor below the table's low edge (same fix as KorenTriode/KorenPentode's plate current, see their
        // comment): the formula below is smooth arbitrarily far into the reverse-biased tail via SoftPlus's own
        // exp(x) asymptote (a real, if minuscule, current and gradient -- "< 1e-20 A" was true but returning an exact
        // zero DERIVATIVE too was not, and a gradient-free port is one Newton has no way to steer back from).
        const auto sp = SoftPlus::of (x);
        const double y = std::max (sp.value / Cg, 1.0e-30);
        const double yPow = std::pow (y, xi - 1.0);
        i = Gg * yPow * y;
        di = Gg * xi * yPow * sp.slope;
    }
};

/**
    Koren's triode ("Improved vacuum-tube models for SPICE simulations", 1996), plus Dempwolf's grid current.
    ip(vgk, vpk) = 2 E1^Ex / Kg1,  E1 = vpk/Kp ln(1 + exp(Kp (1/mu + vgk / sqrt(Kvb + vpk^2)))).
    Defaults are the 12AX7 / ECC83 set (mu 100, Ex 1.4, Kg1 1060, Kp 600, Kvb 300).

    evaluate() is the run-time version: E1^Ex = (vpk/Kp)^Ex * softplus(a)^Ex splits into two one-variable functions
    that come from Hermite tables (see HermiteTable), so a call costs a sqrt and two lookups. evaluateExact() is the
    formula itself (the reference the tests compare against).
*/
class KorenTriode
{
public:
    struct Parameters
    {
        double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
        GridCurrent grid {};
    };

    KorenTriode() { setParameters ({}); }

    void setParameters (const Parameters& p)
    {
        par = p;
        par.grid.prepareTable();
        softplusPower = TubeTables::softplusPower (p.ex);
        plateFactor = TubeTables::plateFactor (p.kp, p.ex);
    }
    const Parameters& parameters() const noexcept { return par; }

    struct Point
    {
        double ip, dip_dvgk, dip_dvpk;
        double ig, dig_dvgk;
    };

    Point evaluate (double vgk, double vpk) const noexcept
    {
        Point o {};
        par.grid.evaluate (vgk, o.ig, o.dig_dvgk);

        // The exact-formula fallbacks below take the RAW plate voltage: evaluateExact() applies the smooth floor itself. Handing it the
        // already floored value applied the floor TWICE, and the second application maps any plate at or below its cathode to
        // 2 ln 2 = 1.39 V, not to ~0: a plate far below its cathode passed the current of a plate at 1.39 V whenever the grid was
        // driven positive (0.5 mA at vgk +5 V, 3.7 mA at +20 V, for any vpk down to -180 V). That phantom conduction is a second,
        // non-physical root for Newton (a phase inverter with both plates below ground and its tail 70 V too high) and is what
        // "converged" solves of a hard-driven tube amplifier were landing on. See docs/circuits/SuperLead1959.md.
        const double vpkRaw = vpk;
        double dVpkFloor;
        floorPlateVoltage (vpk, dVpkFloor); // see its own comment; vpk is now always > 0, so no vpk<=0 case remains below

        if (vpk >= plateFactor->highEdge() - 1.0)
            return evaluateExact (vgk, vpkRaw);

        const double s = std::sqrt (par.kvb + vpk * vpk);
        const double a = par.kp * (1.0 / par.mu + vgk / s);
        // Below the table's low edge (a deeply cut-off grid) used to return zero current AND zero gradient outright --
        // asymmetric with the high-edge branch just below, which already falls back to the exact analytic formula
        // instead of a hard floor. SoftPlus itself is smooth arbitrarily far into the tail (exp(a), never literally
        // zero), so the fix is the same fallback both directions: a real, if tiny, restoring gradient for Newton to
        // follow instead of a dead zone with no gradient at all. Found chasing a Bassman power-stage stall under
        // extreme sustained drive (Volume Normal/Bright + Power Drive all maxed): the failing trace showed one grid
        // port sitting exactly in this dead zone for the whole 100+-iteration walk. See docs/circuits/Bassman5F6A.md.
        if (a < softplusPower->lowEdge() || a > softplusPower->highEdge() - 1.0)
            return evaluateExact (vgk, vpkRaw);

        double f, df, g, dg;
        softplusPower->evaluate (a, f, df);
        plateFactor->evaluate (vpk, g, dg);
        const double k = 2.0 / par.kg1;
        const double dA_dvpk = -par.kp * vgk * vpk / (s * s * s);
        o.ip = k * g * f;
        o.dip_dvgk = k * g * df * (par.kp / s);
        o.dip_dvpk = k * (dg * f + g * df * dA_dvpk) * dVpkFloor;
        return o;
    }

    Point evaluateExact (double vgk, double vpk) const noexcept
    {
        Point o {};
        par.grid.evaluate (vgk, o.ig, o.dig_dvgk);

        double dVpkFloor;
        floorPlateVoltage (vpk, dVpkFloor); // same fix as evaluate() above, kept independent so this stands correct when called directly too

        const double s = std::sqrt (par.kvb + vpk * vpk);
        const double arg = par.kp * (1.0 / par.mu + vgk / s);
        const auto sp = SoftPlus::of (arg);
        const double e1 = vpk / par.kp * sp.value;
        if (e1 <= 0.0)
            return o;

        const double dE1_dvgk = vpk * sp.slope / s;
        const double dE1_dvpk = sp.value / par.kp + vpk / par.kp * sp.slope * (-par.kp * vgk * vpk / (s * s * s));
        const double e1Pow = std::pow (e1, par.ex - 1.0);
        const double k = 2.0 / par.kg1;
        o.ip = k * e1Pow * e1;
        const double dIp_dE1 = k * par.ex * e1Pow;
        o.dip_dvgk = dIp_dE1 * dE1_dvgk;
        o.dip_dvpk = dIp_dE1 * dE1_dvpk * dVpkFloor;
        return o;
    }

private:
    Parameters par;
    std::shared_ptr<const HermiteTable> softplusPower, plateFactor;
};

/**
    Koren's pentode / beam tetrode, evaluated at a given screen voltage (the screen is not a circuit node here: the
    caller supplies vg2, see NodalCircuit::addPentode). Plate current
    ip = 2 E1^Ex / Kg1 atan(vpk/Kvb) (1 + lambda (vpk - vRef)), screen current ig2 = E1^Ex / Kg2,
    E1 = vg2/Kp ln(1 + exp(Kp (1/mu + vgk/vg2))). The (1 + lambda ...) term is the finite plate resistance of a beam
    tetrode that the plain Koren pentode leaves out. Defaults: 6L6GC.
*/
class KorenPentode
{
public:
    struct Parameters
    {
        double mu = 8.7, ex = 1.35, kg1 = 1460.0, kg2 = 4500.0, kp = 48.0, kvb = 12.0;
        double lambda = 6.0e-4, vRef = 400.0;
        // Flash-over: a plate driven far above its rating (an output transformer's inductive kick when a hard-driven
        // stage cuts the tube off) arcs to the cathode. It only ever matters for an absurd overdrive, but it keeps the
        // model physical there instead of letting the plate run to kilovolts.
        double arcVoltage = 1100.0, arcResistance = 25.0;
        GridCurrent grid { 4.5e-4, 8.0, 1.5 };
    };

    KorenPentode() { setParameters ({}); }

    void setParameters (const Parameters& p)
    {
        par = p;
        par.grid.prepareTable();
        softplusPower = TubeTables::softplusPower (p.ex);
        knee = TubeTables::knee (p.kvb);
        screenPower = TubeTables::plateFactor (p.kp, p.ex);
    }
    const Parameters& parameters() const noexcept { return par; }

    struct Point
    {
        double ip, dip_dvgk, dip_dvpk;
        double ig, dig_dvgk;
        double ig2;
    };

    /** (vg2 / Kp)^Ex: constant while the screen voltage is, so callers that evaluate many times per sample hoist it. */
    double screenFactor (double vg2) const noexcept
    {
        if (vg2 <= 1.0)
            return 0.0;
        return vg2 < screenPower->highEdge() - 1.0 ? screenPower->value (vg2) : std::pow (vg2 / par.kp, par.ex);
    }

    Point evaluate (double vgk, double vpk, double vg2) const noexcept { return evaluate (vgk, vpk, vg2, screenFactor (vg2)); }

    Point evaluate (double vgk, double vpk, double vg2, double screenPow) const noexcept
    {
        Point o {};
        par.grid.evaluate (vgk, o.ig, o.dig_dvgk);
        if (vg2 <= 1.0)
            return o;

        const double a = par.kp * (1.0 / par.mu + vgk / vg2);

        // Same fix as KorenTriode::evaluate() (see its comment): both table edges fall back to the exact SoftPlus
        // formula, which is smooth arbitrarily far into either tail, instead of the low edge returning a hard,
        // gradient-free zero the high edge never did.
        double f, df;
        if (a < softplusPower->lowEdge() || a > softplusPower->highEdge() - 1.0)
        {
            const auto sp = SoftPlus::of (a);
            f = std::pow (sp.value, par.ex);
            df = par.ex * std::pow (sp.value, par.ex - 1.0) * sp.slope;
        }
        else
            softplusPower->evaluate (a, f, df);

        o.ig2 = screenPow * f / par.kg2;

        // Same fix as KorenTriode::evaluate()/evaluateExact() (see floorPlateVoltage()'s own comment): a plate below
        // the cathode used to collect exactly zero current with exactly zero gradient, a dead end for Newton to hunt
        // its way out of. ig2 (above) doesn't depend on vpk at all, so it's unaffected either way.
        double dVpkFloor;
        floorPlateVoltage (vpk, dVpkFloor);

        double at, dAt;
        if (vpk > knee->lowEdge() && vpk < knee->highEdge() - 1.0)
            knee->evaluate (vpk, at, dAt);
        else
        {
            at = std::atan (vpk / par.kvb);
            dAt = (1.0 / par.kvb) / (1.0 + (vpk / par.kvb) * (vpk / par.kvb));
        }
        const double fl = 1.0 + par.lambda * (vpk - par.vRef);
        const double k = 2.0 / par.kg1 * screenPow;
        o.ip = k * f * at * fl;
        o.dip_dvgk = k * df * (par.kp / vg2) * at * fl;
        o.dip_dvpk = k * f * (dAt * fl + at * par.lambda);
        if (vpk > par.arcVoltage - 200.0)
        {
            const auto arc = SoftPlus::of ((vpk - par.arcVoltage) / 20.0); // knee 20 V wide
            o.ip += 20.0 * arc.value / par.arcResistance;
            o.dip_dvpk += arc.slope / par.arcResistance;
        }
        o.dip_dvpk *= dVpkFloor;
        return o;
    }

    double screenCurrent (double vgk, double vg2) const noexcept { return evaluate (vgk, 100.0, vg2).ig2; }

private:
    Parameters par;
    std::shared_ptr<const HermiteTable> softplusPower, knee, screenPower;
};

} // namespace openguitarmultifx
