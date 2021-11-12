#pragma once

#include <cstdint>
#include <sstream>

namespace Aurora
{
	struct Color
	{
		union
		{
			struct
			{
				uint8_t r;
				uint8_t g;
				uint8_t b;
				uint8_t a;
			};
			uint32_t rgba;
		};

		Color() {} // This is intention so we don't initialize data automatically

		constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
				: r(r), g(g), b(b), a(a) {}

		constexpr Color(uint32_t rgba)
				: rgba(rgba) {}

		static constexpr Color red() { return {255, 0, 0}; }
		static constexpr Color green() { return {0, 255, 0}; }
		static constexpr Color blue() { return {0, 0, 255}; }
		static constexpr Color black() { return {0, 0, 0}; }
		static constexpr Color white() { return {255, 255, 255}; }
		static constexpr Color zero() { return {0, 0, 0, 0}; }
	};

	static std::ostream& operator<<(std::ostream& os, const Color& color)
	{
		os << (int)color.r << "," << (int)color.g << "," << (int)color.b << "," << (int)color.a;
		return os;
	}
}