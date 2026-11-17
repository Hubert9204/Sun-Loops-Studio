#include "audio/Formula.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sls
{
namespace
{
using Op = Formula::Op;
using Step = Formula::Step;

struct Function
{
    const char* name;
    Op op;
    int arguments;
};
constexpr Function kFunctions[] = {
    { "sin", Op::Sin, 1 },     { "cos", Op::Cos, 1 },     { "tan", Op::Tan, 1 },   { "abs", Op::Abs, 1 },   { "sqrt", Op::Sqrt, 1 },
    { "exp", Op::Exp, 1 },     { "log", Op::Log, 1 },     { "floor", Op::Floor, 1 }, { "ceil", Op::Ceil, 1 }, { "round", Op::Round, 1 },
    { "frac", Op::Frac, 1 },   { "tri", Op::Tri, 1 },     { "saw", Op::Saw, 1 },   { "sqr", Op::Sqr, 1 },   { "rnd", Op::Rnd, 1 },
    { "min", Op::Min, 2 },     { "max", Op::Max, 2 },     { "pow", Op::Pow, 2 },   { "clamp", Op::Clamp, 3 }, { "if", Op::If, 3 },
    { "lerp", Op::Lerp, 3 },
};

// A recursive-descent reader: comparison < sum < product < power < unary < primary. It writes the steps in the order a stack
// machine runs them and keeps count of how deep the stack gets.
struct Parser
{
    explicit Parser (const std::string& toRead) : text (toRead) {}

    const std::string& text;
    size_t pos = 0;
    std::vector<Step> steps;
    std::string error;
    int depth = 0, maxDepth = 0;

    void skipSpace()
    {
        while (pos < text.size() && std::isspace (static_cast<unsigned char> (text[pos])) != 0)
            ++pos;
    }
    bool fail (const std::string& why)
    {
        if (error.empty())
            error = why;
        return false;
    }
    void emit (Op op, float number = 0.0f, int stackChange = 0)
    {
        steps.push_back ({ op, number });
        depth += stackChange;
        maxDepth = std::max (maxDepth, depth);
    }
    bool take (const char* token)
    {
        skipSpace();
        size_t n = 0;
        while (token[n] != '\0')
            ++n;
        if (text.compare (pos, n, token) != 0)
            return false;
        pos += n;
        return true;
    }

    bool comparison()
    {
        if (! sum())
            return false;
        for (;;)
        {
            Op op;
            if (take ("<="))
                op = Op::LessEq;
            else if (take (">="))
                op = Op::GreaterEq;
            else if (take ("=="))
                op = Op::Equal;
            else if (take ("!="))
                op = Op::NotEqual;
            else if (take ("<"))
                op = Op::Less;
            else if (take (">"))
                op = Op::Greater;
            else
                return true;
            if (! sum())
                return false;
            emit (op, 0.0f, -1);
        }
    }
    bool sum()
    {
        if (! product())
            return false;
        for (;;)
        {
            Op op;
            if (take ("+"))
                op = Op::Add;
            else if (take ("-"))
                op = Op::Sub;
            else
                return true;
            if (! product())
                return false;
            emit (op, 0.0f, -1);
        }
    }
    bool product()
    {
        if (! unary())
            return false;
        for (;;)
        {
            Op op;
            if (take ("*"))
                op = Op::Mul;
            else if (take ("/"))
                op = Op::Div;
            else if (take ("%"))
                op = Op::Mod;
            else
                return true;
            if (! unary())
                return false;
            emit (op, 0.0f, -1);
        }
    }
    bool unary()
    {
        if (take ("-"))
        {
            if (! unary())
                return false;
            emit (Op::Neg);
            return true;
        }
        if (take ("+"))
            return unary();
        return power();
    }
    bool power()
    {
        if (! primary())
            return false;
        if (take ("^"))
        {
            if (! unary()) // right-associative, and 2 ^ -1 works
                return false;
            emit (Op::Pow, 0.0f, -1);
        }
        return true;
    }
    bool primary()
    {
        skipSpace();
        if (pos >= text.size())
            return fail ("the formula ends too early");
        const char ch = text[pos];
        if (ch == '(')
        {
            ++pos;
            if (! comparison())
                return false;
            if (! take (")"))
                return fail ("a closing bracket is missing");
            return true;
        }
        if (std::isdigit (static_cast<unsigned char> (ch)) != 0 || ch == '.')
        {
            const char* start = text.c_str() + pos;
            char* end = nullptr;
            const double value = std::strtod (start, &end);
            if (end == start)
                return fail ("a number cannot be read");
            pos += static_cast<size_t> (end - start);
            emit (Op::Number, static_cast<float> (value), 1);
            return true;
        }
        if (std::isalpha (static_cast<unsigned char> (ch)) != 0)
        {
            std::string name;
            while (pos < text.size() && (std::isalnum (static_cast<unsigned char> (text[pos])) != 0 || text[pos] == '_'))
                name += static_cast<char> (std::tolower (static_cast<unsigned char> (text[pos++])));
            if (name == "a") { emit (Op::VarA, 0.0f, 1); return true; }
            if (name == "b") { emit (Op::VarB, 0.0f, 1); return true; }
            if (name == "c") { emit (Op::VarC, 0.0f, 1); return true; }
            if (name == "t" || name == "time") { emit (Op::VarT, 0.0f, 1); return true; }
            if (name == "beat" || name == "beats") { emit (Op::VarBeat, 0.0f, 1); return true; }
            if (name == "level" || name == "peak") { emit (Op::VarLevel, 0.0f, 1); return true; }
            if (name == "pi") { emit (Op::Number, 3.14159265358979f, 1); return true; }
            for (const auto& f : kFunctions)
            {
                if (name != f.name)
                    continue;
                if (! take ("("))
                    return fail (name + " needs its value in brackets");
                for (int k = 0; k < f.arguments; ++k)
                {
                    if (k > 0 && ! take (","))
                        return fail (name + " needs " + std::to_string (f.arguments) + " values");
                    if (! comparison())
                        return false;
                }
                if (! take (")"))
                    return fail ("a closing bracket is missing after " + name);
                emit (f.op, 0.0f, 1 - f.arguments);
                return true;
            }
            return fail ("\"" + name + "\" is not a name the formula knows");
        }
        return fail (std::string ("\"") + ch + "\" is not expected here");
    }
};

inline float fracOf (float x) noexcept { return x - std::floor (x); }

// A random value 0..1 for a whole number (the same number always gives the same value).
inline float hashOf (float x) noexcept
{
    auto n = static_cast<std::uint32_t> (static_cast<std::int64_t> (std::floor (static_cast<double> (x))));
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<float> (n & 0xffffffu) / static_cast<float> (0xffffff);
}
} // namespace

std::shared_ptr<const Formula> Formula::compile (const std::string& text, std::string* error)
{
    Parser p (text);
    bool ok = p.comparison();
    if (ok)
    {
        p.skipSpace();
        if (p.pos < text.size())
            ok = p.fail (std::string ("\"") + text[p.pos] + "\" is not expected here");
    }
    if (ok && static_cast<int> (p.steps.size()) > kMaxSteps)
        ok = p.fail ("the formula is too long");
    if (ok && p.maxDepth > kMaxStack)
        ok = p.fail ("the formula is nested too deeply");
    if (! ok)
    {
        if (error != nullptr)
            *error = p.error;
        return nullptr;
    }
    std::shared_ptr<Formula> f (new Formula());
    f->source = text;
    f->steps = std::move (p.steps);
    return f;
}

float Formula::evaluate (const Variables& v) const noexcept
{
    std::array<float, kMaxStack + 1> stack {};
    int top = 0; // the next free place
    const auto push = [&] (float x) noexcept { stack[static_cast<size_t> (top++)] = x; };
    for (const auto& s : steps)
    {
        float* x = top > 0 ? &stack[static_cast<size_t> (top - 1)] : nullptr; // the last value
        const auto two = [&] (auto&& f) noexcept
        {
            const float right = stack[static_cast<size_t> (--top)];
            float& left = stack[static_cast<size_t> (top - 1)];
            left = f (left, right);
        };
        switch (s.op)
        {
            case Op::Number:   push (s.number); break;
            case Op::VarA:     push (v.a); break;
            case Op::VarB:     push (v.b); break;
            case Op::VarC:     push (v.c); break;
            case Op::VarT:     push (static_cast<float> (v.t)); break;
            case Op::VarBeat:  push (static_cast<float> (v.beat)); break;
            case Op::VarLevel: push (v.level); break;
            case Op::Add:      two ([] (float a, float b) noexcept { return a + b; }); break;
            case Op::Sub:      two ([] (float a, float b) noexcept { return a - b; }); break;
            case Op::Mul:      two ([] (float a, float b) noexcept { return a * b; }); break;
            case Op::Div:      two ([] (float a, float b) noexcept { return b != 0.0f ? a / b : 0.0f; }); break;
            case Op::Mod:      two ([] (float a, float b) noexcept { return b != 0.0f ? a - b * std::floor (a / b) : 0.0f; }); break;
            case Op::Pow:      two ([] (float a, float b) noexcept { return std::pow (a, b); }); break;
            case Op::Less:     two ([] (float a, float b) noexcept { return a < b ? 1.0f : 0.0f; }); break;
            case Op::Greater:  two ([] (float a, float b) noexcept { return a > b ? 1.0f : 0.0f; }); break;
            case Op::LessEq:   two ([] (float a, float b) noexcept { return a <= b ? 1.0f : 0.0f; }); break;
            case Op::GreaterEq: two ([] (float a, float b) noexcept { return a >= b ? 1.0f : 0.0f; }); break;
            case Op::Equal:    two ([] (float a, float b) noexcept { return std::abs (a - b) < 1.0e-6f ? 1.0f : 0.0f; }); break;
            case Op::NotEqual: two ([] (float a, float b) noexcept { return std::abs (a - b) < 1.0e-6f ? 0.0f : 1.0f; }); break;
            case Op::Min:      two ([] (float a, float b) noexcept { return std::min (a, b); }); break;
            case Op::Max:      two ([] (float a, float b) noexcept { return std::max (a, b); }); break;
            case Op::Neg:      *x = -*x; break;
            case Op::Sin:      *x = std::sin (*x); break;
            case Op::Cos:      *x = std::cos (*x); break;
            case Op::Tan:      *x = std::tan (*x); break;
            case Op::Abs:      *x = std::abs (*x); break;
            case Op::Sqrt:     *x = *x > 0.0f ? std::sqrt (*x) : 0.0f; break;
            case Op::Exp:      *x = std::exp (std::min (*x, 80.0f)); break;
            case Op::Log:      *x = *x > 0.0f ? std::log (*x) : 0.0f; break;
            case Op::Floor:    *x = std::floor (*x); break;
            case Op::Ceil:     *x = std::ceil (*x); break;
            case Op::Round:    *x = std::round (*x); break;
            case Op::Frac:     *x = fracOf (*x); break;
            case Op::Tri:      { const float p = fracOf (*x); *x = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p; break; }
            case Op::Saw:      *x = fracOf (*x); break;
            case Op::Sqr:      *x = fracOf (*x) < 0.5f ? 1.0f : 0.0f; break;
            case Op::Rnd:      *x = hashOf (*x); break;
            case Op::Clamp:
            {
                const float high = stack[static_cast<size_t> (--top)], low = stack[static_cast<size_t> (--top)];
                float& value = stack[static_cast<size_t> (top - 1)];
                value = std::min (std::max (value, low), std::max (low, high));
                break;
            }
            case Op::If:
            {
                const float otherwise = stack[static_cast<size_t> (--top)], then = stack[static_cast<size_t> (--top)];
                float& value = stack[static_cast<size_t> (top - 1)];
                value = value != 0.0f ? then : otherwise;
                break;
            }
            case Op::Lerp:
            {
                const float amount = stack[static_cast<size_t> (--top)], to = stack[static_cast<size_t> (--top)];
                float& value = stack[static_cast<size_t> (top - 1)];
                value = value + (to - value) * amount;
                break;
            }
        }
    }
    const float result = top > 0 ? stack[static_cast<size_t> (top - 1)] : 0.0f;
    return std::isfinite (result) ? std::min (std::max (result, 0.0f), 1.0f) : 0.0f;
}
} // namespace sls
