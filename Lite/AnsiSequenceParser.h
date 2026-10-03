#pragma once

// Streaming parser for 7-bit ESC/CSI sequences. No platform or UI dependencies.
// Bytes >= 0x80 remain text (Big5/UAO and UTF-8 must not become C1 controls).
// OSC/DCS string protocols are outside this parser's scope.
class AnsiSequenceParser
{
public:
	enum Result { Text, Consumed, Dispatch, SyncBegin, SyncEnd };
	AnsiSequenceParser() : state(Ground), length(0), supported(true), number(0), overflow(false)
	{
		sequence[0] = 0;
	}

	Result Feed(unsigned char byte)
	{
		if (byte == 0x1b)
		{
			state = Escape;
			length = 0;
			supported = true;
			number = 0;
			overflow = false;
			return Consumed;
		}
		if (byte == 0x18 || byte == 0x1a) // CAN / SUB cancel a sequence.
		{
			state = Ground;
			return Consumed;
		}
		if (state == Ground)
			return Text;
		if (byte == 0x7f)
			return Consumed;
		if (byte < 0x20) // Execute C0 without losing the pending sequence.
			return Text;
		if (byte >= 0x80)
		{
			state = Ground; // Invalid in a 7-bit sequence; preserve text bytes.
			return Text;
		}

		if (state == Escape && byte == '[')
		{
			Append(byte);
			state = Parameters;
			return Consumed;
		}
		if (state == Escape || state == EscapeIntermediate)
		{
			Append(byte);
			if (byte >= 0x20 && byte <= 0x2f)
			{
				state = EscapeIntermediate;
				supported = false;
				return Consumed;
			}
			return Finish();
		}

		// CSI: parameter bytes 0x30..0x3f, intermediates 0x20..0x2f,
		// then exactly one final byte 0x40..0x7e. Unknown/oversized
		// sequences are consumed through the final, never partly executed.
		Append(byte);
		if (byte >= 0x40 && byte <= 0x7e)
			return Finish();
		if (byte >= 0x20 && byte <= 0x2f)
		{
			state = Intermediate;
			supported = false;
		}
		else if (state != Parameters || (byte != ';' && (byte < '0' || byte > '9')))
			supported = false; // Private prefixes, subparameters or invalid order.
		else if (byte == ';')
			number = 0;
		else if (supported)
		{
			number = number * 10 + byte - '0';
			// Bound input before legacy atoi and coordinate/count arithmetic.
			if (number > 32767)
				supported = false;
		}
		return Consumed;
	}

	// Valid only when Feed returns Dispatch, and until the next Feed call.
	const char* Sequence() const { return sequence; }
	unsigned int Length() const { return length; }

private:
	enum State { Ground, Escape, EscapeIntermediate, Parameters, Intermediate };
	State state;
	char sequence[64]; // Includes the '[' for CSI and a trailing NUL.
	unsigned int length;
	bool supported;
	unsigned int number;
	bool overflow;

	void Append(unsigned char byte)
	{
		if (length < sizeof(sequence) - 1)
			sequence[length++] = static_cast<char>(byte);
		else
		{
			supported = false;
			overflow = true;
		}
	}

	Result Finish()
	{
		state = Ground;
		sequence[length] = 0;
		// Recognize ONLY this private mode, never forward private parameters
		// to the legacy numeric dispatcher. Overlong prefixes cannot qualify.
		if (!overflow && length == 7 && sequence[0] == '[' &&
			sequence[1] == '?' && sequence[2] == '2' && sequence[3] == '0' &&
			sequence[4] == '2' && sequence[5] == '6')
		{
			if (sequence[6] == 'h') return SyncBegin;
			if (sequence[6] == 'l') return SyncEnd;
		}
		if (!supported)
			return Consumed;
		// Only the existing command family reaches the legacy dispatcher.
		const char finalByte = sequence[length - 1];
		const char* commands = sequence[0] == '[' ? "mKHfrJELABCDsu@MPZhln" : "DME78";
		for (const char* command = commands; *command; ++command)
			if (*command == finalByte)
				return Dispatch;
		return Consumed;
	}
};
