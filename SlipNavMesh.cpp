#include "SlipNavMesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <cstring>
#include <assert.h>

#include <SlipLevel.h>

struct BoundsItem
{
	float bmin[2];
	float bmax[2];
	int i;
};

static int compareItemX(const void* va, const void* vb)
{
	const BoundsItem* a = (const BoundsItem*)va;
	const BoundsItem* b = (const BoundsItem*)vb;
	if (a->bmin[0] < b->bmin[0])
		return -1;
	if (a->bmin[0] > b->bmin[0])
		return 1;
	return 0;
}

static int compareItemY(const void* va, const void* vb)
{
	const BoundsItem* a = (const BoundsItem*)va;
	const BoundsItem* b = (const BoundsItem*)vb;
	if (a->bmin[1] < b->bmin[1])
		return -1;
	if (a->bmin[1] > b->bmin[1])
		return 1;
	return 0;
}

static void calcExtends(const BoundsItem* items, const int /*nitems*/,
	const int imin, const int imax,
	float* bmin, float* bmax)
{
	bmin[0] = items[imin].bmin[0];
	bmin[1] = items[imin].bmin[1];

	bmax[0] = items[imin].bmax[0];
	bmax[1] = items[imin].bmax[1];

	for (int i = imin + 1; i < imax; ++i)
	{
		const BoundsItem& it = items[i];
		if (it.bmin[0] < bmin[0]) bmin[0] = it.bmin[0];
		if (it.bmin[1] < bmin[1]) bmin[1] = it.bmin[1];

		if (it.bmax[0] > bmax[0]) bmax[0] = it.bmax[0];
		if (it.bmax[1] > bmax[1]) bmax[1] = it.bmax[1];
	}
}

inline int longestAxis(float x, float y)
{
	return y > x ? 1 : 0;
}

static void subdivide(BoundsItem* items, int nitems, int imin, int imax, int trisPerChunk,
	int& curNode, rcChunkyTriMeshNode* nodes, const int maxNodes,
	int& curTri, int* outTris, const int* inTris)
{
	int inum = imax - imin;
	int icur = curNode;

	if (curNode >= maxNodes)
		return;

	rcChunkyTriMeshNode& node = nodes[curNode++];

	if (inum <= trisPerChunk)
	{
		// Leaf
		calcExtends(items, nitems, imin, imax, node.bmin, node.bmax);

		// Copy triangles.
		node.i = curTri;
		node.n = inum;

		for (int i = imin; i < imax; ++i)
		{
			const int* src = &inTris[items[i].i * 3];
			int* dst = &outTris[curTri * 3];
			curTri++;
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
		}
	}
	else
	{
		// Split
		calcExtends(items, nitems, imin, imax, node.bmin, node.bmax);

		int	axis = longestAxis(node.bmax[0] - node.bmin[0],
			node.bmax[1] - node.bmin[1]);

		if (axis == 0)
		{
			// Sort along x-axis
			qsort(items + imin, static_cast<size_t>(inum), sizeof(BoundsItem), compareItemX);
		}
		else if (axis == 1)
		{
			// Sort along y-axis
			qsort(items + imin, static_cast<size_t>(inum), sizeof(BoundsItem), compareItemY);
		}

		int isplit = imin + inum / 2;

		// Left
		subdivide(items, nitems, imin, isplit, trisPerChunk, curNode, nodes, maxNodes, curTri, outTris, inTris);
		// Right
		subdivide(items, nitems, isplit, imax, trisPerChunk, curNode, nodes, maxNodes, curTri, outTris, inTris);

		int iescape = curNode - icur;
		// Negative index means escape.
		node.i = -iescape;
	}
}

bool rcCreateChunkyTriMesh(const float* verts, const int* tris, int ntris,
	int trisPerChunk, rcChunkyTriMesh* cm)
{
	int nchunks = (ntris + trisPerChunk - 1) / trisPerChunk;

	cm->nodes = new rcChunkyTriMeshNode[nchunks * 4];
	if (!cm->nodes)
		return false;

	cm->tris = new int[ntris * 3];
	if (!cm->tris)
		return false;

	cm->ntris = ntris;

	// Build tree
	BoundsItem* items = new BoundsItem[ntris];
	if (!items)
		return false;

	for (int i = 0; i < ntris; i++)
	{
		const int* t = &tris[i * 3];
		BoundsItem& it = items[i];
		it.i = i;
		// Calc triangle XZ bounds.
		it.bmin[0] = it.bmax[0] = verts[t[0] * 3 + 0];
		it.bmin[1] = it.bmax[1] = verts[t[0] * 3 + 2];
		for (int j = 1; j < 3; ++j)
		{
			const float* v = &verts[t[j] * 3];
			if (v[0] < it.bmin[0]) it.bmin[0] = v[0];
			if (v[2] < it.bmin[1]) it.bmin[1] = v[2];

			if (v[0] > it.bmax[0]) it.bmax[0] = v[0];
			if (v[2] > it.bmax[1]) it.bmax[1] = v[2];
		}
	}

	int curTri = 0;
	int curNode = 0;
	subdivide(items, ntris, 0, ntris, trisPerChunk, curNode, cm->nodes, nchunks * 4, curTri, cm->tris, tris);

	delete[] items;

	cm->nnodes = curNode;

	// Calc max tris per node.
	cm->maxTrisPerChunk = 0;
	for (int i = 0; i < cm->nnodes; ++i)
	{
		rcChunkyTriMeshNode& node = cm->nodes[i];
		const bool isLeaf = node.i >= 0;
		if (!isLeaf) continue;
		if (node.n > cm->maxTrisPerChunk)
			cm->maxTrisPerChunk = node.n;
	}

	return true;
}


inline bool checkOverlapRect(const float amin[2], const float amax[2],
	const float bmin[2], const float bmax[2])
{
	bool overlap = true;
	overlap = (amin[0] > bmax[0] || amax[0] < bmin[0]) ? false : overlap;
	overlap = (amin[1] > bmax[1] || amax[1] < bmin[1]) ? false : overlap;
	return overlap;
}

int rcGetChunksOverlappingRect(const rcChunkyTriMesh* cm,
	float bmin[2], float bmax[2],
	int* ids, const int maxIds)
{
	// Traverse tree
	int i = 0;
	int n = 0;
	while (i < cm->nnodes)
	{
		const rcChunkyTriMeshNode* node = &cm->nodes[i];
		const bool overlap = checkOverlapRect(bmin, bmax, node->bmin, node->bmax);
		const bool isLeafNode = node->i >= 0;

		if (isLeafNode && overlap)
		{
			if (n < maxIds)
			{
				ids[n] = i;
				n++;
			}
		}

		if (overlap || isLeafNode)
			i++;
		else
		{
			const int escapeIndex = -node->i;
			i += escapeIndex;
		}
	}

	return n;
}



static bool checkOverlapSegment(const float p[2], const float q[2],
	const float bmin[2], const float bmax[2])
{
	static const float EPSILON = 1e-6f;

	float tmin = 0;
	float tmax = 1;
	float d[2];
	d[0] = q[0] - p[0];
	d[1] = q[1] - p[1];

	for (int i = 0; i < 2; i++)
	{
		if (fabsf(d[i]) < EPSILON)
		{
			// Ray is parallel to slab. No hit if origin not within slab
			if (p[i] < bmin[i] || p[i] > bmax[i])
				return false;
		}
		else
		{
			// Compute intersection t value of ray with near and far plane of slab
			float ood = 1.0f / d[i];
			float t1 = (bmin[i] - p[i]) * ood;
			float t2 = (bmax[i] - p[i]) * ood;
			if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
			if (t1 > tmin) tmin = t1;
			if (t2 < tmax) tmax = t2;
			if (tmin > tmax) return false;
		}
	}
	return true;
}

int rcGetChunksOverlappingSegment(const rcChunkyTriMesh* cm,
	float p[2], float q[2],
	int* ids, const int maxIds)
{
	// Traverse tree
	int i = 0;
	int n = 0;
	while (i < cm->nnodes)
	{
		const rcChunkyTriMeshNode* node = &cm->nodes[i];
		const bool overlap = checkOverlapSegment(p, q, node->bmin, node->bmax);
		const bool isLeafNode = node->i >= 0;

		if (isLeafNode && overlap)
		{
			if (n < maxIds)
			{
				ids[n] = i;
				n++;
			}
		}

		if (overlap || isLeafNode)
			i++;
		else
		{
			const int escapeIndex = -node->i;
			i += escapeIndex;
		}
	}

	return n;
}

SlipNavMesh::SlipNavMesh() : m_ctx(new rcContext())
						, m_cfg(new rcConfig())
						, m_solid(new rcHeightfield())
						, m_chf(new rcCompactHeightfield())
						, m_cset(new rcContourSet())
						, m_pmesh(new rcPolyMesh())
						, m_dmesh(new rcPolyMeshDetail())
						, m_chunkyTriMesh(new rcChunkyTriMesh())
{
	assert(!m_Instance && "SlipNavMesh has initialized...");
	m_Instance = this;

	memset(m_cfg, 0, sizeof(rcConfig));
	m_cfg->cs = 0.3f;
	m_cfg->ch = 0.2f;
	m_cfg->walkableSlopeAngle = 45.0f;
	m_cfg->walkableHeight = 2.0f;
	m_cfg->walkableClimb = 0.9f;
	m_cfg->walkableRadius = 0.3f;
	m_cfg->maxEdgeLen = 12;
	m_cfg->maxSimplificationError = 1.3f;
	m_cfg->minRegionArea = 8;
	m_cfg->mergeRegionArea = 20;
	m_cfg->detailSampleDist = 6.0f;
	m_cfg->detailSampleMaxError = 1.0f;
}

SlipNavMesh::~SlipNavMesh()
{
	delete m_ctx;
	delete m_cfg;
	delete m_solid;
	delete m_chf;
	delete m_cset;
	delete m_pmesh;
	delete m_dmesh;
	delete m_chunkyTriMesh;
}

void SlipNavMesh::init()
{
	float* vertexs = new float[SlipLevel::Get().bsp->coll.vertices.size()*3];
	int* indexes = new int[SlipLevel::Get().bsp->coll.indices.size()];

	for (int i = 0; i < SlipLevel::Get().bsp->coll.vertices.size(); i++)
	{
		vertexs[i] = SlipLevel::Get().bsp->coll.vertices[i].Position.x;
		vertexs[i+1] = SlipLevel::Get().bsp->coll.vertices[i].Position.y;
		vertexs[i+2] = SlipLevel::Get().bsp->coll.vertices[i].Position.z;
	}

	for (int i = 0; i < SlipLevel::Get().bsp->coll.indices.size(); i++)
	{
		indexes[i] = SlipLevel::Get().bsp->coll.indices[i];
	}

	dtNavMeshParams* params = new dtNavMeshParams();

	dtNavMesh* mesh = dtAllocNavMesh();
	mesh->init(params);

	rcCalcBounds(vertexs, SlipLevel::Get().bsp->coll.vertices.size() * 3, m_cfg->bmin, m_cfg->bmax);

	// Crear campo de altura
	m_solid = rcAllocHeightfield();
	rcCreateHeightfield(m_ctx, *m_solid, m_cfg->width, m_cfg->height, m_cfg->bmin, m_cfg->bmax, m_cfg->cs, m_cfg->ch);

	// Agregar terreno al campo de altura
	//rcAddTriangleMesh(m_ctx, vertexs, SlipLevel::Get().bsp->coll.vertices.size() * 3, indexes, SlipLevel::Get().bsp->coll.indices.size());

	// Filtrar obstáculos
	rcFilterLowHangingWalkableObstacles(m_ctx, m_cfg->walkableClimb, *m_solid);
	rcFilterLedgeSpans(m_ctx, m_cfg->walkableHeight, m_cfg->walkableClimb, *m_solid);
	rcFilterWalkableLowHeightSpans(m_ctx, m_cfg->walkableHeight, *m_solid);

	// Crear campo de altura compacto
	m_chf = rcAllocCompactHeightfield();
	rcBuildCompactHeightfield(m_ctx, m_cfg->walkableHeight, m_cfg->walkableClimb, *m_solid, *m_chf);

	// Crear contornos
	m_cset = rcAllocContourSet();
	rcBuildContours(m_ctx, *m_chf, m_cfg->maxSimplificationError, m_cfg->maxEdgeLen, *m_cset);

	// Crear poly mesh
	m_pmesh = rcAllocPolyMesh();
	rcBuildPolyMesh(m_ctx, *m_cset, m_cfg->maxVertsPerPoly, *m_pmesh);

	// Crear detalle de poly mesh
	m_dmesh = rcAllocPolyMeshDetail();
	rcBuildPolyMeshDetail(m_ctx, *m_pmesh, *m_chf, m_cfg->detailSampleDist, m_cfg->detailSampleMaxError, *m_dmesh);

	// Crear tri mesh
	m_chunkyTriMesh = new rcChunkyTriMesh();
	rcCreateChunkyTriMesh(vertexs, indexes, SlipLevel::Get().bsp->coll.indices.size() / 3, 256, m_chunkyTriMesh);

	ioNav* io = new ioNav("navmesh.obj");
	duDumpPolyMeshToObj(*m_pmesh, io);

	io->out->close();

	// Liberar recursos
	rcFreeHeightField(m_solid);
	rcFreeCompactHeightfield(m_chf);
	rcFreeContourSet(m_cset);
}

ioNav::ioNav(const char* file)
{
	out = new std::ofstream(file, std::ios::out | std::ios::app);

	if (out->is_open())
	{
		writing = true;
	}
}

ioNav::~ioNav()
{
}

bool ioNav::write(const void* ptr, const size_t size)
{
	out->write(reinterpret_cast<char const*>(&ptr), size);
	return true;
}
