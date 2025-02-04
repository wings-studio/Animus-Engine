#pragma once

#include "Aurora/Core/String.hpp"
#include "AnimationChannel.hpp"

namespace Aurora::Animation
{
	struct FAnimation
	{
		String Name;
		double Duration;
		double TicksPerSecond;
		std::vector<AnimationChannel> Channels;

		FAnimation() : Name(), Duration(0), TicksPerSecond(0) {}
		FAnimation(String name, double duration, double tickPerSecond) : Name(std::move(name)), Duration(duration), TicksPerSecond(tickPerSecond) {}
	};
}