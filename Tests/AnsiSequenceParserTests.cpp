#include "../Lite/AnsiSequenceParser.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static unsigned int checks = 0;
static void Check(bool success, const char* label)
{
	++checks;
	if (!success)
	{
		std::fprintf(stderr, "FAIL: %s (check %u)\n", label, checks);
		std::exit(1);
	}
}

struct Capture
{
	AnsiSequenceParser parser;
	std::string text;
	std::vector<std::string> commands;
	void Feed(const std::string& bytes)
	{
		for (unsigned char byte : bytes)
		{
			const auto result = parser.Feed(byte);
			if (result == AnsiSequenceParser::Text)
				text += static_cast<char>(byte);
			else if (result == AnsiSequenceParser::Dispatch)
			{
				Check(parser.Length() < 64, "bounded command");
				Check(parser.Sequence()[parser.Length()] == 0, "terminated command");
				commands.emplace_back(parser.Sequence(), parser.Length());
			}
		}
	}
};

// Run every possible two-packet split, plus one byte per packet. The parser
// instance must persist exactly as it does between OnText/ProcessData calls.
static void Expect(const std::string& input, const std::string& text,
	const std::vector<std::string>& commands, const char* label)
{
	for (size_t split = 0; split <= input.size(); ++split)
	{
		Capture capture;
		capture.Feed(input.substr(0, split));
		capture.Feed(input.substr(split));
		Check(capture.text == text, label);
		Check(capture.commands == commands, label);
	}
	Capture capture;
	for (char byte : input)
		capture.Feed(std::string(1, byte));
	Check(capture.text == text, label);
	Check(capture.commands == commands, label);
}

int main()
{
	Expect("plain\r\ntext", "plain\r\ntext", {}, "plain text");
	Expect("\x1b[0;1;37;44mTITLE\x1b[0;30;47mRIGHT\x1b[m", "TITLERIGHT",
		{ "[0;1;37;44m", "[0;30;47m", "[m" }, "PTT header colors");
	const std::string finals = "KHfrJELABCDsu@MPZhln";
	for (char finalByte : finals)
	{
		const std::string command = std::string("[2") + finalByte;
		Expect("\x1b" + command + "OK", "OK", { command }, "existing CSI family");
	}
	Expect("\x1b[12;34H\x1b[;H\x1b[H", "", { "[12;34H", "[;H", "[H" }, "positions/defaults");
	Expect("\x1b" "7\x1b" "8\x1b" "D\x1b" "M\x1b" "E", "",
		{ "7", "8", "D", "M", "E" }, "legacy ESC commands");
	Expect("\x1b[?2026hHELLO\x1b[?2026l", "HELLO", {}, "DEC2026 ignored");
	for (const char* mode : { "1000", "1002", "1003", "1006" })
		Expect(std::string("\x1b[?") + mode + "hX\x1b[?" + mode + "l", "X", {}, "mouse ignored");
	Expect("\x1b[6nOK", "OK", { "[6n" }, "CPR reaches existing no-op handler");
	Expect("\x1b[31mRED\x1b[>4;2mSTILL RED", "REDSTILL RED", { "[31m" }, "private m cannot reset color");
	for (char prefix : std::string("<=>?"))
		for (char finalByte = '@'; finalByte <= '~'; ++finalByte)
			Expect(std::string("\x1b[") + prefix + "1;2" + finalByte + "OK", "OK", {}, "all private finals ignored");
	for (char intermediate = 0x20; intermediate <= 0x2f; ++intermediate)
		Expect(std::string("\x1b[1") + intermediate + "AOK", "OK", {}, "intermediate cannot move cursor");
	Expect("\x1b[1 2AOK", "OK", {}, "invalid parameter order");
	Expect("\x1b[38:2:255:0:0mOK", "OK", {}, "unsupported colon subparameters");
	Expect("\x1b[1qOK", "OK", {}, "unknown final");
	Expect("\x1b(BO\x1b MK", "OK", {}, "ESC intermediates ignored");
	const std::string largest = "[" + std::string(61, '0') + "m";
	Expect("\x1b" + largest, "", { largest }, "exact buffer boundary");
	Expect("\x1b[" + std::string(62, '0') + "mOK\x1b[32m", "OK", { "[32m" }, "overflow recovery");
	Expect("\x1b[" + std::string(200, ';') + "HOK", "OK", {}, "long sequence ignored");
	Expect("\x1b[32767C", "", { "[32767C" }, "numeric boundary");
	Expect("\x1b[32768COK\x1b[999999999999999999m!", "OK!", {}, "numeric overflow");
	Expect("\x1b[31\x1b[32mOK", "OK", { "[32m" }, "ESC restart");
	Expect("\x1b\x1b[32mOK", "OK", { "[32m" }, "repeated ESC");
	Expect("\x1b[31\x18OK\x1b[32\x1a!", "OK!", {}, "CAN/SUB cancellation");
	Expect("\x1b[3\r\n\t\b\a1mX", "\r\n\t\b\aX", { "[31m" }, "C0 during CSI");
	Expect(std::string("\x1b[3\0", 4) + "1mX", std::string(1, '\0') + "X", { "[31m" }, "NUL during CSI");
	Expect("\x1b[3\x7f" "1mX", "X", { "[31m" }, "DEL during CSI");
	const std::string encoded = "\xa4\xa4\xa4\xe5\xef\xbe\x9b";
	Expect(encoded, encoded, {}, "Big5/UTF8 bytes remain text");
	Expect("\x1b[31" + encoded + "OK", encoded + "OK", {}, "invalid high-byte recovery");
	Expect("\x1b[31", "", {}, "unfinished sequence does not dispatch");
	Capture first, second;
	first.Feed("\x1b[3");
	second.Feed("\x1b[32mB");
	first.Feed("1mA");
	Check(first.commands == std::vector<std::string>{ "[31m" } && first.text == "A", "connection isolation A");
	Check(second.commands == std::vector<std::string>{ "[32m" } && second.text == "B", "connection isolation B");
	std::printf("PASS: %u checks (all split points and bytewise input)\n", checks);
	return 0;
}
