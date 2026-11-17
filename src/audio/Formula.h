#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sls
{
// A formula of the Formula Controller (0.70.0, the counterpart of Fruity Formula Controller): text such as
// "0.5 + 0.5 * sin(2 * pi * beat) * a" is compiled once, on the message thread, into a short list of steps that the audio thread
// runs without allocating.
//
//   numbers            1, 0.25, .5
//   operators          + - * / % ^, unary -, < > <= >= == != (1 when true, 0 when false), brackets
//   variables          a b c    the controller's three knobs, 0..1
//                      t        seconds since the controller started
//                      beat     the song's position in beats (its own clock at 120 BPM while the song is stopped)
//                      level    the peak level of the controller's track, 0..1
//                      pi
//   functions          sin cos tan abs sqrt exp log floor ceil round frac     one argument
//                      tri saw sqr     waves with period 1 that swing between 0 and 1 (tri(0) = 0, saw(0) = 0, sqr(0) = 1)
//                      rnd(x)          a random value 0..1 that changes each time x passes a whole number
//                      min max pow     two arguments
//                      clamp(x, low, high)   if(condition, then, otherwise)   lerp(from, to, x)
//
// The result is clamped to 0..1 (not a number: 0).
class Formula
{
public:
    struct Variables
    {
        float a = 0.0f, b = 0.0f, c = 0.0f;
        double t = 0.0, beat = 0.0;
        float level = 0.0f;
    };

    static constexpr int kMaxSteps = 256;
    static constexpr int kMaxStack = 32;

    // Null (and the reason in `error`, when given) for text that cannot be read: an unknown name, a missing bracket, too long.
    static std::shared_ptr<const Formula> compile (const std::string& text, std::string* error = nullptr);

    const std::string& text() const noexcept { return source; }
    float evaluate (const Variables& v) const noexcept;

    enum class Op : std::uint8_t
    {
        Number, VarA, VarB, VarC, VarT, VarBeat, VarLevel,
        Add, Sub, Mul, Div, Mod, Pow, Neg, Less, Greater, LessEq, GreaterEq, Equal, NotEqual,
        Sin, Cos, Tan, Abs, Sqrt, Exp, Log, Floor, Ceil, Round, Frac, Tri, Saw, Sqr, Rnd,
        Min, Max, Clamp, If, Lerp
    };
    struct Step
    {
        Op op = Op::Number;
        float number = 0.0f;
    };

private:
    Formula() = default;
    std::string source;
    std::vector<Step> steps;
};
} // namespace sls
