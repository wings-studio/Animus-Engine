#pragma once

#include "../ActorComponent.hpp"
#include "../Transform.hpp"
#include "Aurora/Physics/AABB.hpp"
#include "Aurora/Physics/Types.hpp"

namespace Aurora
{
	class AU_API ColliderComponent : public ActorComponent
	{
	protected:
		AABB m_Bounds;
		Vector3 m_Origin;
	public:
		CLASS_OBJ(ColliderComponent, ActorComponent);

		ColliderComponent() : m_Bounds(), m_Origin(0.0f) {}

		virtual void GetAabb(const Transform& transform, phVector3& aabbMin, phVector3& aabbMax) const
		{
			AABB transformed = m_Bounds.Transform(transform.GetTransform());
			aabbMin = transformed.GetMin();
			aabbMax = transformed.GetMax();
		}

		virtual void GetBoundingSphere(phVector3& center, phScalar& radius) const
		{
			phVector3 aabbMin = m_Bounds.GetMin();
			phVector3 aabbMax = m_Bounds.GetMax();

			radius = glm::length(aabbMax - aabbMin) * phScalar(0.5);
			center = (aabbMin + aabbMax) * phScalar(0.5);
		}

		inline void SetOrigin(const Vector3& origin) { m_Origin = origin; }
		inline const Vector3& GetOrigin() const { return m_Origin; }
		inline Vector3& GetOrigin() { return m_Origin; }

		[[nodiscard]] const AABB& GetAABB() const { return m_Bounds; }
		[[nodiscard]] AABB GetTransformedAABB() const;
	};

	class ProxyColliderComponent : public ColliderComponent
	{
	public:
		CLASS_OBJ(ProxyColliderComponent, ColliderComponent);

		virtual bool CollideWith(const AABB& bounds, const AABB& encapsulatedBounds, Vector3& velocity, double updateRate, uint8_t axis) = 0;
	};

	class BoxColliderComponent : public ColliderComponent
	{
	private:
		Vector3 m_Size;
	public:
		CLASS_OBJ(BoxColliderComponent, ColliderComponent);

		BoxColliderComponent() : ColliderComponent(), m_Size(1, 1, 1)
		{
			SetSize(1, 1, 1);
		}

		BoxColliderComponent(const Vector3& size) : ColliderComponent()
		{
			SetSize(size);
		}

		BoxColliderComponent(float x, float y, float z) : ColliderComponent()
		{
			SetSize(x, y, z);
		}

		inline void SetSize(const Vector3& size)
		{
			m_Size = size;
			m_Bounds = AABB::FromExtent(m_Origin, size / 2.0f);
		}

		inline void SetSize(float x, float y, float z)
		{
			SetSize({x, y, z});
		}

	};
}
