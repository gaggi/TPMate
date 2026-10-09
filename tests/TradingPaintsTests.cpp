// Includes the implementation to check the XML handling without network requests.
#include "TradingPaintsClient.cpp"

#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }

    bool Parse(const std::string& text, std::vector<PaintFile>& paints)
    {
        return XmlToPaints(std::vector<unsigned char>(text.begin(), text.end()), 42, paints);
    }
}

int main()
{
    try
    {
        const std::string car = "<Car><file>https://x/car.tga.bz2</file><directory>car</directory><type>car</type><userid>42</userid></Car>";
        const std::string helmet = "<Car><file>https://x/helmet.tga.bz2</file><type>helmet</type><userid>42</userid></Car>";
        const std::string complete = "<?xml version=\"1.0\"?><Cars>" + car + helmet + "</Cars>";
        std::vector<PaintFile> paints;

        Require(Parse(complete, paints) && paints.size() == 2, "Complete answer");
        Require(paints[0].type == PaintType::Car && paints[0].carPath == "car" && paints[1].type == PaintType::Helmet, "Paint fields");

        paints.clear();
        Require(!Parse(complete.substr(0, complete.find(helmet) + 20), paints), "Answer cut after the first paint is an error");
        paints.clear();
        Require(!Parse(complete.substr(0, 60), paints), "Answer cut inside the first paint is an error");
        paints.clear();
        Require(!Parse("<Cars><Car></Cars>", paints), "Mismatched tags are an error");

        paints.clear();
        Require(Parse("<?xml version=\"1.0\"?><Cars></Cars>", paints) && paints.empty(), "Valid answer without paints");
        paints.clear();
        Require(!Parse("", paints), "Empty body is not parsed");

        std::cout << "Trading Paints checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
