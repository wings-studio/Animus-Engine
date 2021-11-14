#pragma once

#include <vector>
#include "Color.hpp"
#include "Base/IRenderDevice.hpp"
#include "Aurora/Physics/AABB.hpp"

namespace Aurora
{
	class Scene;

	namespace ShapeStructs
	{
		struct ShapeBase
		{
			::Aurora::Color Color;
			float Thickness;
			uint32_t LifeTime;
			bool Wireframe : 1;
			bool UseDepthBuffer : 1;
		};

		struct LineShape : ShapeBase
		{
			Vector3 P0;
			Vector3 P1;
		};

		struct BoxShape : ShapeBase
		{
			AABB Bounds;
		};

		struct SphereShape : ShapeBase
		{
			Vector3 Position;
			float Radius;
		};

		struct ArrowShape : ShapeBase
		{
			Vector3 Position;
			Vector3 Direction;
		};
	}

	class DShapes
	{
	private:
		static std::vector<ShapeStructs::LineShape> m_LineShapes;
		static std::vector<ShapeStructs::BoxShape> m_BoxShapes;
		static std::vector<ShapeStructs::SphereShape> m_SphereShapes;
		static std::vector<ShapeStructs::ArrowShape> m_ArrowShapes;
	public:
		static void Init();
		static void Render(DrawCallState& drawState);
		static void Destroy();
		static void Reset();

		inline static void Line(const Vector3& p0, const Vector3& p1, Color color = Color::green(), float thickness = 1.0, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			ShapeStructs::LineShape shape;
			shape.P0 = p0;
			shape.P1 = p1;

			shape.Color = color;
			shape.Thickness = thickness;
			shape.LifeTime = lifetime;
			shape.Wireframe = false;
			shape.UseDepthBuffer = useDepthBuffer;
			m_LineShapes.emplace_back(shape);
		}

		inline static void Line(const Matrix4& transform, const Vector3& p0, const Vector3& p1, Color color = Color::green(), float thickness = 1.0f, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			Line(Vector3(transform * Vector4(p0, 1.0f)), Vector3(transform * Vector4(p1, 1.0f)), color, thickness, lifetime, useDepthBuffer);
		}

		inline static void Box(const AABB& aabb, Color color = Color::green(), bool wireframe = false, float thickness = 1.0f, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			if(wireframe)
			{
				const Vector3& min = aabb.GetMin();
				const Vector3& max = aabb.GetMax();

				// Corners
				Line(min, Vector3(min.x, max.y, min.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(min.x, min.y, max.z), Vector3(min.x, max.y, max.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, thickness, lifetime, useDepthBuffer);

				// Bottom ring
				Line(min, Vector3(max.x, min.y, min.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(max.x, min.y, max.z), Vector3(min.x, min.y, max.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(min.x, min.y, max.z), Vector3(min.x, min.y, min.z), color, thickness, lifetime, useDepthBuffer);

				// Top ring
				Line(Vector3(min.x, max.y, min.z), Vector3(max.x, max.y, min.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, thickness, lifetime, useDepthBuffer);
				Line(Vector3(min.x, max.y, max.z), Vector3(min.x, max.y, min.z), color, thickness, lifetime, useDepthBuffer);

				return;
			}

			ShapeStructs::BoxShape shape;
			shape.Bounds = aabb;

			shape.Color = color;
			shape.Thickness = thickness;
			shape.LifeTime = lifetime;
			shape.Wireframe = wireframe;
			shape.UseDepthBuffer = useDepthBuffer;
			m_BoxShapes.emplace_back(shape);
		}

		inline static void Box(const Matrix4& transform, AABB aabb, Color color = Color::green(), bool wireframe = false, float thickness = 1.0f, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			aabb *= transform;
			Box(aabb, color, wireframe, thickness, lifetime, useDepthBuffer);
		}

		inline static void Sphere(const Vector3& pos, float radius, Color color = Color::green(), bool wireframe = false, float thickness = 1.0f, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			ShapeStructs::SphereShape shape;
			shape.Position = pos;
			shape.Radius = radius;

			shape.Color = color;
			shape.Thickness = thickness;
			shape.LifeTime = lifetime;
			shape.Wireframe = wireframe;
			shape.UseDepthBuffer = useDepthBuffer;
			m_SphereShapes.emplace_back(shape);
		}

		inline static void Sphere(const Matrix4& transform, const Vector3& pos, float radius, Color color = Color::green(), bool wireframe = false, float thickness = 1.0f, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			Sphere(Vector3(transform * Vector4(pos, 1.0f)), radius, color, wireframe, thickness, lifetime, useDepthBuffer);
		}

		inline static void Arrow(const Vector3& pos, const Vector3& dir, Color color = Color::green(), bool wireframe = false, float thickness = 1.0f, uint32_t lifetime = 0, bool useDepthBuffer = true)
		{
			ShapeStructs::ArrowShape shape;
			shape.Position = pos;
			shape.Direction = dir;

			shape.Color = color;
			shape.Thickness = thickness;
			shape.LifeTime = lifetime;
			shape.Wireframe = wireframe;
			shape.UseDepthBuffer = useDepthBuffer;
			m_ArrowShapes.emplace_back(shape);
		}
	};
}