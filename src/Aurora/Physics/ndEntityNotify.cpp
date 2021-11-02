#include "ndEntityNotify.hpp"

namespace Aurora
{

	ndEntityNotify::ndEntityNotify(TransformComponent* transformComponent) : ndBodyNotify(dVector(0.0f, -10.0f, 0.0f, 0.0f)), m_Transform(transformComponent)
	{

	}

	void ndEntityNotify::OnApplyExternalForce(dInt32 threadIndex, dFloat32 timestep)
	{
		ndBodyDynamic* const dynamicBody = GetBody()->GetAsBodyDynamic();
		if (dynamicBody)
		{
			dVector massMatrix(dynamicBody->GetMassMatrix());
			dVector force(GetGravity().Scale(massMatrix.m_w));
			dynamicBody->SetForce(force);
			dynamicBody->SetTorque(dVector::m_zero);
		}
	}

	void ndEntityNotify::OnTransform(dInt32 threadIndex, const dMatrix &matrix)
	{
		/*m_Transform->Translation = Vector3(matrix.m_posit.m_x, matrix.m_posit.m_y, matrix.m_posit.m_z);

		dQuaternion rot(matrix);

		dVector euler0;
		dVector euler1;
		matrix.CalcPitchYawRoll(euler0, euler1);

		m_Transform->Rotation = Vector3(glm::degrees(euler1.m_x), glm::degrees(euler1.m_y), glm::degrees(euler1.m_z));*/

		m_Transform->SetFromMatrix(glm::make_mat4(&matrix.m_front.m_x));
	}
}