#include "JsonLite.h"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace jsonlite
{
    bool Value::IsObject() const { return std::holds_alternative<Object>(data); }
    bool Value::IsArray() const { return std::holds_alternative<Array>(data); }
    bool Value::IsString() const { return std::holds_alternative<std::string>(data); }
    const Object& Value::AsObject() const { return std::get<Object>(data); }
    const Array& Value::AsArray() const { return std::get<Array>(data); }
    const std::string& Value::AsString() const { return std::get<std::string>(data); }

    namespace
    {
        class Parser
        {
        public:
            explicit Parser(const std::string& input) : input_(input) {}

            Value ParseDocument()
            {
                auto result = ParseValue();
                SkipWhitespace();
                if (index_ != input_.size()) throw std::runtime_error("Trailing JSON data.");
                return result;
            }

            Value ParseValue()
            {
                if (depth_ >= 128) throw std::runtime_error("JSON nesting limit exceeded.");
                struct DepthGuard
                {
                    unsigned& depth;
                    explicit DepthGuard(unsigned& value) : depth(value) { ++depth; }
                    ~DepthGuard() { --depth; }
                } guard(depth_);
                SkipWhitespace();
                if (index_ >= input_.size())
                {
                    throw std::runtime_error("Unexpected end of JSON.");
                }

                switch (input_[index_])
                {
                case '{': return ParseObject();
                case '[': return ParseArray();
                case '"': return Value(ParseString());
                case 't': ConsumeLiteral("true"); return Value(true);
                case 'f': ConsumeLiteral("false"); return Value(false);
                case 'n': ConsumeLiteral("null"); return Value();
                default: return Value(ParseNumber());
                }
            }

        private:
            unsigned ParseHexCodeUnit()
            {
                unsigned value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (index_ == input_.size()) throw std::runtime_error("Incomplete Unicode escape.");
                    const char ch = input_[index_++];
                    value <<= 4;
                    if (ch >= '0' && ch <= '9') value += ch - '0';
                    else if (ch >= 'a' && ch <= 'f') value += ch - 'a' + 10;
                    else if (ch >= 'A' && ch <= 'F') value += ch - 'A' + 10;
                    else throw std::runtime_error("Invalid Unicode escape.");
                }
                return value;
            }

            void ParseUnicode(std::string& result)
            {
                unsigned value = ParseHexCodeUnit();
                if (value >= 0xD800 && value <= 0xDBFF)
                {
                    Expect('\\');
                    Expect('u');
                    const auto low = ParseHexCodeUnit();
                    if (low < 0xDC00 || low > 0xDFFF) throw std::runtime_error("Invalid surrogate pair.");
                    value = 0x10000 + ((value - 0xD800) << 10) + low - 0xDC00;
                }
                else if (value >= 0xDC00 && value <= 0xDFFF) throw std::runtime_error("Unpaired surrogate.");
                if (value < 0x80) result.push_back(static_cast<char>(value));
                else
                {
                    if (value >= 0x10000) result.push_back(static_cast<char>(0xF0 | (value >> 18)));
                    if (value >= 0x800) result.push_back(static_cast<char>(
                        (value >= 0x10000 ? 0x80 : 0xE0) | ((value >> 12) & 0x3F)));
                    result.push_back(static_cast<char>((value >= 0x800 ? 0x80 : 0xC0) | ((value >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                }
            }

            Value ParseObject()
            {
                Expect('{');
                Object object;
                SkipWhitespace();
                if (TryConsume('}'))
                {
                    return object;
                }

                while (true)
                {
                    SkipWhitespace();
                    const auto key = ParseString();
                    SkipWhitespace();
                    Expect(':');
                    object.emplace(key, ParseValue());
                    SkipWhitespace();
                    if (TryConsume('}'))
                    {
                        break;
                    }

                    Expect(',');
                }

                return object;
            }

            Value ParseArray()
            {
                Expect('[');
                Array array;
                SkipWhitespace();
                if (TryConsume(']'))
                {
                    return array;
                }

                while (true)
                {
                    array.push_back(ParseValue());
                    SkipWhitespace();
                    if (TryConsume(']'))
                    {
                        break;
                    }

                    Expect(',');
                }

                return array;
            }

            std::string ParseString()
            {
                Expect('"');
                std::string result;
                while (index_ < input_.size())
                {
                    const char ch = input_[index_++];
                    if (ch == '"')
                    {
                        return result;
                    }

                    if (ch == '\\')
                    {
                        if (index_ >= input_.size())
                        {
                            throw std::runtime_error("Unexpected end of JSON escape.");
                        }

                        const char escaped = input_[index_++];
                        switch (escaped)
                        {
                        case '"': result.push_back('"'); break;
                        case '\\': result.push_back('\\'); break;
                        case '/': result.push_back('/'); break;
                        case 'b': result.push_back('\b'); break;
                        case 'f': result.push_back('\f'); break;
                        case 'n': result.push_back('\n'); break;
                        case 'r': result.push_back('\r'); break;
                        case 't': result.push_back('\t'); break;
                        case 'u': ParseUnicode(result); break;
                        default: throw std::runtime_error("Unsupported JSON escape.");
                        }
                    }
                    else
                    {
                        if (static_cast<unsigned char>(ch) < 0x20) throw std::runtime_error("Unescaped control character.");
                        result.push_back(ch);
                    }
                }

                throw std::runtime_error("Unterminated JSON string.");
            }

            double ParseNumber()
            {
                const size_t start = index_;
                TryConsume('-');
                const auto digit = [this]() { return index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9'; };
                const auto digits = [&]()
                {
                    if (!digit()) throw std::runtime_error("Invalid JSON number.");
                    while (digit()) ++index_;
                };
                if (!TryConsume('0')) digits();
                if (TryConsume('.')) digits();
                if (TryConsume('e') || TryConsume('E'))
                {
                    if (!TryConsume('+')) TryConsume('-');
                    digits();
                }
                double value = 0;
                const auto parsed = std::from_chars(input_.data() + start, input_.data() + index_, value);
                if (parsed.ec != std::errc{} || parsed.ptr != input_.data() + index_ || !std::isfinite(value))
                    throw std::runtime_error("Invalid JSON number.");
                return value;
            }

            void ConsumeLiteral(const char* literal)
            {
                while (*literal != '\0')
                {
                    Expect(*literal++);
                }
            }

            void SkipWhitespace()
            {
                while (index_ < input_.size() && (input_[index_] == ' ' || input_[index_] == '\t' ||
                    input_[index_] == '\r' || input_[index_] == '\n'))
                {
                    ++index_;
                }
            }

            void Expect(char expected)
            {
                if (index_ >= input_.size() || input_[index_] != expected)
                {
                    throw std::runtime_error("Unexpected JSON token.");
                }

                ++index_;
            }

            bool TryConsume(char expected)
            {
                if (index_ < input_.size() && input_[index_] == expected)
                {
                    ++index_;
                    return true;
                }

                return false;
            }

            const std::string& input_;
            size_t index_{0};
            unsigned depth_{0};
        };
    }

    Value Parse(const std::string& input)
    {
        Parser parser(input);
        return parser.ParseDocument();
    }
}
