#pragma once

#include <complex>
#include <utility>
#include <vector>

namespace sls
{
// In-place iterative radix-2 FFT of a power-of-two length.
class Fft
{
public:
    explicit Fft (int sizeIn) : size (sizeIn), twiddle (static_cast<size_t> (sizeIn / 2))
    {
        for (int i = 0; i < size / 2; ++i)
            twiddle[static_cast<size_t> (i)] = std::polar (1.0, -2.0 * 3.14159265358979323846 * i / size);
    }

    void forward (std::vector<std::complex<double>>& x) const { run (x); }

    void inverse (std::vector<std::complex<double>>& x) const
    {
        for (auto& v : x)
            v = std::conj (v);
        run (x);
        for (auto& v : x)
            v = std::conj (v) / static_cast<double> (size);
    }

private:
    void run (std::vector<std::complex<double>>& x) const
    {
        for (int i = 1, j = 0; i < size; ++i)
        {
            int bit = size >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (x[static_cast<size_t> (i)], x[static_cast<size_t> (j)]);
        }
        for (int len = 2; len <= size; len <<= 1)
        {
            const int step = size / len;
            for (int i = 0; i < size; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    const auto u = x[static_cast<size_t> (i + k)];
                    const auto v = x[static_cast<size_t> (i + k + len / 2)] * twiddle[static_cast<size_t> (k * step)];
                    x[static_cast<size_t> (i + k)] = u + v;
                    x[static_cast<size_t> (i + k + len / 2)] = u - v;
                }
        }
    }

    int size;
    std::vector<std::complex<double>> twiddle;
};
} // namespace sls
