#pragma once

#include <assimp/Importer.hpp>

#include "Aurora/Core/Types.hpp"
#include "Aurora/Core/String.hpp"

#include "Aurora/Framework/Mesh/Mesh.hpp"

namespace Aurora
{
	struct MeshImportOptions
	{
		bool SplitMeshes = false;
		bool MergeSameMaterialMeshes = false;
		bool PreTransform = true;
		bool KeepCPUData = false;
		bool UploadToGPU = true;
	};

	struct MeshImportedData
	{
		bool Imported = false;
		Mesh_ptr Mesh = nullptr;
		std::vector<Mesh_ptr> Meshes = {};

		explicit operator bool() const
		{
			return Imported;
		}
	};

	class AssimpModelLoader
	{
	private:
		Assimp::Importer m_Importer;
	public:
		MeshImportedData ImportModel(const String& name, const DataBlob& data, const MeshImportOptions& importOptions = {});
	};
}
