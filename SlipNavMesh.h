#pragma once

#include <Recast.h>
#include <RecastAlloc.h>
#include <RecastAssert.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourCommon.h>

#include <RecastDump.h>

#include <fstream>


struct rcChunkyTriMeshNode
{
	float bmin[2];
	float bmax[2];
	int i;
	int n;
};

struct rcChunkyTriMesh
{
	inline rcChunkyTriMesh() : nodes(0), nnodes(0), tris(0), ntris(0), maxTrisPerChunk(0) {}
	inline ~rcChunkyTriMesh() { delete[] nodes; delete[] tris; }

	rcChunkyTriMeshNode* nodes;
	int nnodes;
	int* tris;
	int ntris;
	int maxTrisPerChunk;

private:
	// Explicitly disabled copy constructor and copy assignment operator.
	rcChunkyTriMesh(const rcChunkyTriMesh&);
	rcChunkyTriMesh& operator=(const rcChunkyTriMesh&);
};

/// Creates partitioned triangle mesh (AABB tree),
/// where each node contains at max trisPerChunk triangles.
bool rcCreateChunkyTriMesh(const float* verts, const int* tris, int ntris,
	int trisPerChunk, rcChunkyTriMesh* cm);

/// Returns the chunk indices which overlap the input rectable.
int rcGetChunksOverlappingRect(const rcChunkyTriMesh* cm, float bmin[2], float bmax[2], int* ids, const int maxIds);

/// Returns the chunk indices which overlap the input segment.
int rcGetChunksOverlappingSegment(const rcChunkyTriMesh* cm, float p[2], float q[2], int* ids, const int maxIds);

struct ioNav : duFileIO
{
	std::ofstream* out;
	bool writing = false;

	ioNav(const char* file);
	~ioNav();

	bool isWriting() const override { return writing; }
	bool isReading() const override { return false; }
	bool write(const void* ptr, const size_t size);
	bool read(void* ptr, const size_t size) { return false; }
};

class SlipNavMesh
{
private:
	rcContext* m_ctx;
	rcConfig* m_cfg;
	rcHeightfield* m_solid;
	rcCompactHeightfield* m_chf;
	rcContourSet* m_cset;
	rcPolyMesh* m_pmesh;
	rcPolyMeshDetail* m_dmesh;
	rcChunkyTriMesh* m_chunkyTriMesh;

	inline static SlipNavMesh* m_Instance;
public:
	inline static SlipNavMesh& Get() { return *m_Instance; }

	SlipNavMesh();
	~SlipNavMesh();

	void init();
};

