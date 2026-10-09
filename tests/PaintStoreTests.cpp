#include "PaintStore.h"
#include "bzlib.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }

    std::vector<unsigned char> Compress(const std::vector<char>& raw)
    {
        std::vector<char> packed(raw.size() + raw.size() / 100 + 600);
        unsigned int size = static_cast<unsigned int>(packed.size());
        if (BZ2_bzBuffToBuffCompress(packed.data(), &size, const_cast<char*>(raw.data()), static_cast<unsigned int>(raw.size()), 9, 0, 0) != BZ_OK)
            throw std::runtime_error("Could not create test data");
        return {packed.begin(), packed.begin() + size};
    }

    // Runs the decompression on a worker so a regression (an endless loop) fails the test
    // instead of hanging it.
    bool DecompressWithin(const std::vector<unsigned char>& input, std::vector<unsigned char>& output,
        const std::atomic_bool& stopping, const char* message)
    {
        auto result = std::async(std::launch::async, [&] { return PaintStore::DecompressBzip2(input, output, stopping); });
        if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        {
            std::cerr << message << ": did not return\n";
            TerminateProcess(GetCurrentProcess(), 1);
        }
        return result.get();
    }
}

int main()
{
    try
    {
        std::vector<char> raw(3 * 1024 * 1024);
        for (size_t index = 0; index < raw.size(); ++index) raw[index] = static_cast<char>((index * 2654435761u) >> 13);
        const auto complete = Compress(raw);
        std::atomic_bool stopping{false};
        std::vector<unsigned char> output;

        Require(DecompressWithin(complete, output, stopping, "Complete stream") && output.size() == raw.size() &&
            std::equal(output.begin(), output.end(), reinterpret_cast<const unsigned char*>(raw.data())), "Complete stream round-trips");

        const std::vector<unsigned char> half(complete.begin(), complete.begin() + complete.size() / 2);
        Require(!DecompressWithin(half, output, stopping, "Truncated stream") && output.empty(), "Truncated stream is rejected");

        const std::vector<unsigned char> last(complete.begin(), complete.end() - 1);
        Require(!DecompressWithin(last, output, stopping, "Stream missing its last byte"), "Stream missing its last byte is rejected");

        std::vector<unsigned char> damaged = complete;
        for (size_t index = 100; index < 200; ++index) damaged[index] ^= 0x5A;
        Require(!DecompressWithin(damaged, output, stopping, "Damaged stream"), "Damaged stream is rejected");

        Require(!DecompressWithin({'n', 'o', 't', ' ', 'b', 'z', '2'}, output, stopping, "Not bzip2"), "Other data is rejected");

        std::atomic_bool stopped{true};
        Require(!DecompressWithin(complete, output, stopped, "Stopping"), "Stopping cancels decompression");

        std::cout << "Paint store checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
