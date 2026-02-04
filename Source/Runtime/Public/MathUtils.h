#pragma once
#include "MathTypes.h"

namespace won::math
{
	inline constexpr float4x4 IDENTITY_MATRIX = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	inline constexpr float PI = XM_PI;

	template<typename T>
	constexpr T align(T value, T alignment)
	{
		return ((value + alignment - T(1)) / alignment) * alignment;
	}

	template <typename T>
	constexpr T sqr(T x) { return x * x; }

	template <typename T>
	constexpr T clamp(T x, T a, T b)
	{
		return x < a ? a : (x > b ? b : x);
	}

	template <typename T>
	constexpr float Lerp(T x, T y, T a)
	{
		return x * (1 - a) + y * a;
	}

	template <typename T>
	constexpr T inverse_lerp(T value1, T value2, T pos)
	{
		return value2 == value1 ? T(0) : ((pos - value1) / (value2 - value1));
	}

	inline bool float_equal(float f1, float f2) {
		return (std::abs(f1 - f2) <= std::numeric_limits<float>::epsilon() * std::max(std::abs(f1), std::abs(f2)));
	}

	constexpr float saturate(float x)
	{
		return clamp(x, 0.f, 1.f);
	}

	inline float LengthSquared(const float2& v)
	{
		return v.x * v.x + v.y * v.y;
	}
	inline float LengthSquared(const float3& v)
	{
		return v.x * v.x + v.y * v.y + v.z * v.z;
	}
	inline float Length(const float2& v)
	{
		return std::sqrt(LengthSquared(v));
	}
	inline float Length(const float3& v)
	{
		return std::sqrt(LengthSquared(v));
	}
	inline float Distance(XMVECTOR v1, XMVECTOR v2)
	{
		return XMVectorGetX(XMVector3Length(XMVectorSubtract(v1, v2)));
	}
	inline float DistanceSquared(XMVECTOR v1, XMVECTOR v2)
	{
		return XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(v1, v2)));
	}
	inline float DistanceEstimated(const XMVECTOR& v1, const XMVECTOR& v2)
	{
		XMVECTOR vectorSub = XMVectorSubtract(v1, v2);
		XMVECTOR length = XMVector3LengthEst(vectorSub);

		float Distance = 0.0f;
		XMStoreFloat(&Distance, length);
		return Distance;
	}
	inline float Dot(const float2& v1, const float2& v2)
	{
		XMVECTOR vector1 = XMLoadFloat2(&v1);
		XMVECTOR vector2 = XMLoadFloat2(&v2);
		return XMVectorGetX(XMVector2Dot(vector1, vector2));
	}
	inline float Dot(const float3& v1, const float3& v2)
	{
		XMVECTOR vector1 = XMLoadFloat3(&v1);
		XMVECTOR vector2 = XMLoadFloat3(&v2);
		return XMVectorGetX(XMVector3Dot(vector1, vector2));
	}
	inline float Distance(const float2& v1, const float2& v2)
	{
		XMVECTOR vector1 = XMLoadFloat2(&v1);
		XMVECTOR vector2 = XMLoadFloat2(&v2);
		return XMVectorGetX(XMVector2Length(vector2 - vector1));
	}
	inline float Distance(const float3& v1, const float3& v2)
	{
		XMVECTOR vector1 = XMLoadFloat3(&v1);
		XMVECTOR vector2 = XMLoadFloat3(&v2);
		return Distance(vector1, vector2);
	}
	inline float DistanceSquared(const float2& v1, const float2& v2)
	{
		XMVECTOR vector1 = XMLoadFloat2(&v1);
		XMVECTOR vector2 = XMLoadFloat2(&v2);
		return XMVectorGetX(XMVector2LengthSq(vector2 - vector1));
	}
	inline float DistanceSquared(const float3& v1, const float3& v2)
	{
		XMVECTOR vector1 = XMLoadFloat3(&v1);
		XMVECTOR vector2 = XMLoadFloat3(&v2);
		return DistanceSquared(vector1, vector2);
	}
	inline float DistanceSquared(const XMVECTOR& v1, const float3& v2)
	{
		XMVECTOR vector2 = XMLoadFloat3(&v2);
		return DistanceSquared(v1, vector2);
	}
	inline float DistanceSquared(const float3& v1, const XMVECTOR& v2)
	{
		XMVECTOR vector1 = XMLoadFloat3(&v1);
		return DistanceSquared(vector1, v2);
	}
	inline float DistanceEstimated(const float2& v1, const float2& v2)
	{
		XMVECTOR vector1 = XMLoadFloat2(&v1);
		XMVECTOR vector2 = XMLoadFloat2(&v2);
		return XMVectorGetX(XMVector2LengthEst(vector2 - vector1));
	}
	inline float DistanceEstimated(const float3& v1, const float3& v2)
	{
		XMVECTOR vector1 = XMLoadFloat3(&v1);
		XMVECTOR vector2 = XMLoadFloat3(&v2);
		return DistanceEstimated(vector1, vector2);
	}
	inline XMVECTOR ClosestPointOnLine(const XMVECTOR& A, const XMVECTOR& B, const XMVECTOR& Point)
	{
		XMVECTOR AB = B - A;
		XMVECTOR T = XMVector3Dot(Point - A, AB) / XMVector3Dot(AB, AB);
		return A + T * AB;
	}
	inline XMVECTOR ClosestPointOnLineSegment(const XMVECTOR& A, const XMVECTOR& B, const XMVECTOR& Point)
	{
		XMVECTOR AB = B - A;
		XMVECTOR T = XMVector3Dot(Point - A, AB) / XMVector3Dot(AB, AB);
		return A + XMVectorSaturate(T) * AB;
	}
	constexpr float3 getVectorHalfWayPoint(const float3& a, const float3& b)
	{
		return float3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
	}
	inline XMVECTOR InverseLerp(XMVECTOR value1, XMVECTOR value2, XMVECTOR pos)
	{
		return (pos - value1) / (value2 - value1);
	}
	constexpr float InverseLerp(float value1, float value2, float pos)
	{
		return inverse_lerp(value1, value2, pos);
	}
	constexpr float2 InverseLerp(const float2& value1, const float2& value2, const float2& pos)
	{
		return float2(InverseLerp(value1.x, value2.x, pos.x), InverseLerp(value1.y, value2.y, pos.y));
	}
	constexpr float3 InverseLerp(const float3& value1, const float3& value2, const float3& pos)
	{
		return float3(InverseLerp(value1.x, value2.x, pos.x), InverseLerp(value1.y, value2.y, pos.y), InverseLerp(value1.z, value2.z, pos.z));
	}
	constexpr float4 InverseLerp(const float4& value1, const float4& value2, const float4& pos)
	{
		return float4(InverseLerp(value1.x, value2.x, pos.x), InverseLerp(value1.y, value2.y, pos.y), InverseLerp(value1.z, value2.z, pos.z), InverseLerp(value1.w, value2.w, pos.w));
	}
	inline XMVECTOR Lerp(XMVECTOR value1, XMVECTOR value2, XMVECTOR amount)
	{
		return value1 + (value2 - value1) * amount;
	}
	//constexpr float Lerp(float value1, float value2, float amount)
	//{
	//	return Lerp(value1, value2, amount);
	//}
	constexpr float2 Lerp(const float2& a, const float2& b, float i)
	{
		return float2(Lerp(a.x, b.x, i), Lerp(a.y, b.y, i));
	}
	constexpr float3 Lerp(const float3& a, const float3& b, float i)
	{
		return float3(Lerp(a.x, b.x, i), Lerp(a.y, b.y, i), Lerp(a.z, b.z, i));
	}
	constexpr float4 Lerp(const float4& a, const float4& b, float i)
	{
		return float4(Lerp(a.x, b.x, i), Lerp(a.y, b.y, i), Lerp(a.z, b.z, i), Lerp(a.w, b.w, i));
	}
	constexpr float2 Lerp(const float2& a, const float2& b, const float2& i)
	{
		return float2(Lerp(a.x, b.x, i.x), Lerp(a.y, b.y, i.y));
	}
	constexpr float3 Lerp(const float3& a, const float3& b, const float3& i)
	{
		return float3(Lerp(a.x, b.x, i.x), Lerp(a.y, b.y, i.y), Lerp(a.z, b.z, i.z));
	}
	constexpr float4 Lerp(const float4& a, const float4& b, const float4& i)
	{
		return float4(Lerp(a.x, b.x, i.x), Lerp(a.y, b.y, i.y), Lerp(a.z, b.z, i.z), Lerp(a.w, b.w, i.w));
	}
	inline float4 Slerp(const float4& a, const float4& b, float i)
	{
		XMVECTOR _a = XMLoadFloat4(&a);
		XMVECTOR _b = XMLoadFloat4(&b);
		XMVECTOR result = XMQuaternionSlerp(_a, _b, i);
		float4 retVal;
		XMStoreFloat4(&retVal, result);
		return retVal;
	}
	constexpr float2 Max(const float2& a, const float2& b) {
		return float2(std::max(a.x, b.x), std::max(a.y, b.y));
	}
	constexpr float3 Max(const float3& a, const float3& b) {
		return float3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
	}
	constexpr float4 Max(const float4& a, const float4& b) {
		return float4(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z), std::max(a.w, b.w));
	}
	constexpr float2 Min(const float2& a, const float2& b) {
		return float2(std::min(a.x, b.x), std::min(a.y, b.y));
	}
	constexpr float3 Min(const float3& a, const float3& b) {
		return float3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
	}
	constexpr float4 Min(const float4& a, const float4& b) {
		return float4(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), std::min(a.w, b.w));
	}
	constexpr float2 Abs(const float2& a) {
		return float2(std::abs(a.x), std::abs(a.y));
	}
	constexpr float3 Abs(const float3& a) {
		return float3(std::abs(a.x), std::abs(a.y), std::abs(a.z));
	}
	constexpr float4 Abs(const float4& a) {
		return float4(std::abs(a.x), std::abs(a.y), std::abs(a.z), std::abs(a.w));
	}
	constexpr float Clamp(float val, float min, float max)
	{
		return std::min(max, std::max(min, val));
	}
	constexpr float2 Clamp(float2 val, float2 min, float2 max)
	{
		float2 retval = val;
		retval.x = Clamp(retval.x, min.x, max.x);
		retval.y = Clamp(retval.y, min.y, max.y);
		return retval;
	}
	constexpr float3 Clamp(float3 val, float3 min, float3 max)
	{
		float3 retval = val;
		retval.x = Clamp(retval.x, min.x, max.x);
		retval.y = Clamp(retval.y, min.y, max.y);
		retval.z = Clamp(retval.z, min.z, max.z);
		return retval;
	}
	constexpr float4 Clamp(float4 val, float4 min, float4 max)
	{
		float4 retval = val;
		retval.x = Clamp(retval.x, min.x, max.x);
		retval.y = Clamp(retval.y, min.y, max.y);
		retval.z = Clamp(retval.z, min.z, max.z);
		retval.w = Clamp(retval.w, min.w, max.w);
		return retval;
	}
	constexpr float SmoothStep(float value1, float value2, float amount)
	{
		amount = Clamp((amount - value1) / (value2 - value1), 0.0f, 1.0f);
		return amount * amount * amount * (amount * (amount * 6 - 15) + 10);
	}
	constexpr bool Collision2D(const float2& hitBox1Pos, const float2& hitBox1Siz, const float2& hitBox2Pos, const float2& hitBox2Siz)
	{
		if (hitBox1Siz.x <= 0 || hitBox1Siz.y <= 0 || hitBox2Siz.x <= 0 || hitBox2Siz.y <= 0)
			return false;

		if (hitBox1Pos.x + hitBox1Siz.x < hitBox2Pos.x)
			return false;
		else if (hitBox1Pos.x > hitBox2Pos.x + hitBox2Siz.x)
			return false;
		else if (hitBox1Pos.y + hitBox1Siz.y < hitBox2Pos.y)
			return false;
		else if (hitBox1Pos.y > hitBox2Pos.y + hitBox2Siz.y)
			return false;

		return true;
	}
	constexpr uint GetNextPowerOfTwo(uint x)
	{
		--x;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		return ++x;
	}
	constexpr uint64_t GetNextPowerOfTwo(uint64_t x)
	{
		--x;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		x |= x >> 32u;
		return ++x;
	}

	// A uniform 2D random generator for hemisphere sampling: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
	//	idx	: iteration index
	//	num	: number of iterations in total
	constexpr float2 Hammersley2D(uint idx, uint num) {
		uint bits = idx;
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		const float radicalInverse_VdC = float(bits) * 2.3283064365386963e-10f; // / 0x100000000

		return float2(float(idx) / float(num), radicalInverse_VdC);
	}
	inline XMMATRIX GetTangentSpace(const float3& N)
	{
		// Choose a helper vector for the cross product
		XMVECTOR helper = std::abs(N.x) > 0.99 ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(1, 0, 0, 0);

		// Generate vectors
		XMVECTOR normal = XMLoadFloat3(&N);
		XMVECTOR tangent = XMVector3Normalize(XMVector3Cross(normal, helper));
		XMVECTOR binormal = XMVector3Normalize(XMVector3Cross(normal, tangent));
		return XMMATRIX(tangent, binormal, normal, XMVectorSet(0, 0, 0, 1));
	}

	constexpr float SphereSurfaceArea(float radius)
	{
		return 4 * PI * radius * radius;
	}
	constexpr float SphereVolume(float radius)
	{
		return 4.0f / 3.0f * PI * radius * radius * radius;
	}

	inline XMVECTOR GetQuadraticBezierPos(const XMVECTOR& a, const XMVECTOR& b, const XMVECTOR& c, float t)
	{
		// XMVECTOR optimized version
		const float param0 = sqr(1 - t);
		const float param1 = 2 * (1 - t) * t;
		const float param2 = sqr(t);
		const XMVECTOR param = XMVectorSet(param0, param1, param2, 1);
		const XMMATRIX M = XMMATRIX(a, b, c, XMVectorSet(0, 0, 0, 1));
		return XMVector3TransformNormal(param, M);
	}

	// Centripetal Catmull-Rom avoids self intersections that can appear with XMVectorCatmullRom
	//	But it doesn't support the case when p0 == p1 or p2 == p3!
	//	This also supports tension to control curve smoothness
	//	Note: Catmull-Rom interpolates between p1 and p2 by value of t
	inline XMVECTOR XM_CALLCONV CatmullRomCentripetal(XMVECTOR p0, XMVECTOR p1, XMVECTOR p2, XMVECTOR p3, float t, float tension = 0.5f)
	{
		float alpha = 1.0f - tension;
		float t0 = 0.0f;
		float t1 = t0 + std::pow(DistanceEstimated(p0, p1), alpha);
		float t2 = t1 + std::pow(DistanceEstimated(p1, p2), alpha);
		float t3 = t2 + std::pow(DistanceEstimated(p2, p3), alpha);
		t = Lerp(t1, t2, t);
		float t1t0 = 1.0f / std::max(0.001f, t1 - t0);
		float t2t1 = 1.0f / std::max(0.001f, t2 - t1);
		float t3t2 = 1.0f / std::max(0.001f, t3 - t2);
		float t2t0 = 1.0f / std::max(0.001f, t2 - t0);
		float t3t1 = 1.0f / std::max(0.001f, t3 - t1);
		XMVECTOR A1 = (t1 - t) * t1t0 * p0 + (t - t0) * t1t0 * p1;
		XMVECTOR A2 = (t2 - t) * t2t1 * p1 + (t - t1) * t2t1 * p2;
		XMVECTOR A3 = (t3 - t) * t3t2 * p2 + (t - t2) * t3t2 * p3;
		XMVECTOR B1 = (t2 - t) * t2t0 * A1 + (t - t0) * t2t0 * A2;
		XMVECTOR B2 = (t3 - t) * t3t1 * A2 + (t - t1) * t3t1 * A3;
		XMVECTOR C = (t2 - t) * t2t1 * B1 + (t - t1) * t2t1 * B2;
		return C;
	}

	inline float GetPointSegmentDistance(const XMVECTOR& point, const XMVECTOR& segmentA, const XMVECTOR& segmentB)
	{
		// Return minimum distance between line segment vw and point p
		const float l2 = XMVectorGetX(XMVector3LengthSq(segmentB - segmentA));  // i.e. |w-v|^2 -  avoid a sqrt
		if (l2 == 0.0) return Distance(point, segmentA);   // v == w case
		// Consider the line extending the segment, parameterized as v + t (w - v).
		// We find projection of point p onto the line. 
		// It falls where t = [(p-v) . (w-v)] / |w-v|^2
		// We clamp t from [0,1] to handle points outside the segment vw.
		const float t = std::max(0.0f, std::min(1.0f, XMVectorGetX(XMVector3Dot(point - segmentA, segmentB - segmentA)) / l2));
		const XMVECTOR projection = segmentA + t * (segmentB - segmentA);  // Projection falls on the segment
		return Distance(point, projection);
	}

	inline float GetPlanePointDistance(const XMVECTOR& planeOrigin, const XMVECTOR& planeNormal, const XMVECTOR& point)
	{
		return XMVectorGetX(XMVector3Dot(planeNormal, point - planeOrigin));
	}

	constexpr float RadiansToDegrees(float radians) { return radians / XM_PI * 180.0f; }
	constexpr float DegreesToRadians(float degrees) { return degrees / 180.0f * XM_PI; }

	inline float3 GetPosition(const float4x4& _m)
	{
		return *((float3*)&_m._41);
	}
	inline float3 GetForward(const float4x4& _m)
	{
		return float3(_m.m[2][0], _m.m[2][1], _m.m[2][2]);
	}
	inline float3 GetUp(const float4x4& _m)
	{
		return float3(_m.m[1][0], _m.m[1][1], _m.m[1][2]);
	}
	inline float3 GetRight(const float4x4& _m)
	{
		return float3(_m.m[0][0], _m.m[0][1], _m.m[0][2]);
	}

	inline XMVECTOR GetPosition(const XMMATRIX& M)
	{
		float4x4 _m;
		XMStoreFloat4x4(&_m, M);
		float3 ret = GetPosition(_m);
		return XMLoadFloat3(&ret);
	}
	inline XMVECTOR GetForward(const XMMATRIX& M)
	{
		float4x4 _m;
		XMStoreFloat4x4(&_m, M);
		float3 ret = GetForward(_m);
		return XMLoadFloat3(&ret);
	}
	inline XMVECTOR GetUp(const XMMATRIX& M)
	{
		float4x4 _m;
		XMStoreFloat4x4(&_m, M);
		float3 ret = GetUp(_m);
		return XMLoadFloat3(&ret);
	}
	inline XMVECTOR GetRight(const XMMATRIX& M)
	{
		float4x4 _m;
		XMStoreFloat4x4(&_m, M);
		float3 ret = GetRight(_m);
		return XMLoadFloat3(&ret);
	}

	inline uint pack_half2(float x, float y)
	{
		return (uint)XMConvertFloatToHalf(x) | ((uint)XMConvertFloatToHalf(y) << 16u);
	}
	inline uint pack_half2(const float2& value)
	{
		return pack_half2(value.x, value.y);
	}
	inline uint2 pack_half3(float x, float y, float z)
	{
		return uint2(
			(uint)XMConvertFloatToHalf(x) | ((uint)XMConvertFloatToHalf(y) << 16u),
			(uint)XMConvertFloatToHalf(z)
		);
	}
	inline uint2 pack_half3(const float3& value)
	{
		return pack_half3(value.x, value.y, value.z);
	}
	inline uint2 pack_half4(float x, float y, float z, float w)
	{
		return uint2(
			(uint)XMConvertFloatToHalf(x) | ((uint)XMConvertFloatToHalf(y) << 16u),
			(uint)XMConvertFloatToHalf(z) | ((uint)XMConvertFloatToHalf(w) << 16u)
		);
	}
	inline uint2 pack_half4(const float4& value)
	{
		return pack_half4(value.x, value.y, value.z, value.w);
	}


	constexpr uint pack_unorm16x2(float x, float y)
	{
		return uint(saturate(x) * 65535.0f) | (uint(saturate(y) * 65535.0f) << 16u);
	}
	constexpr uint pack_unorm16x2(float2 value)
	{
		return pack_unorm16x2(value.x, value.y);
	}
	constexpr uint2 pack_unorm16x4(float x, float y, float z, float w)
	{
		return uint2(pack_unorm16x2(x, y), pack_unorm16x2(z, w));
	}
	constexpr uint2 pack_unorm16x4(float4 value)
	{
		return pack_unorm16x4(value.x, value.y, value.z, value.w);
	}


	//-----------------------------------------------------------------------------
	// Compute the intersection of a ray (Origin, Direction) with a triangle
	// (V0, V1, V2).  Return true if there is an intersection and also set *pDist
	// to the distance along the ray to the intersection.
	//
	// The algorithm is based on Moller, Tomas and Trumbore, "Fast, Minimum Storage
	// Ray-Triangle Intersection", Journal of Graphics Tools, vol. 2, no. 1,
	// pp 21-28, 1997.
	//
	//	Modified for WickedEngine to return barycentrics and support TMin, TMax
	//-----------------------------------------------------------------------------
	_Use_decl_annotations_
		inline bool XM_CALLCONV RayTriangleIntersects(
			FXMVECTOR Origin,
			FXMVECTOR Direction,
			FXMVECTOR V0,
			GXMVECTOR V1,
			HXMVECTOR V2,
			float& Dist,
			float2& bary,
			float TMin = 0,
			float TMax = std::numeric_limits<float>::max()
		)
	{
		//const XMVECTOR g_RayEpsilon = XMVectorSet(1e-20f, 1e-20f, 1e-20f, 1e-20f);
		//const XMVECTOR g_RayNegEpsilon = XMVectorSet(-1e-20f, -1e-20f, -1e-20f, -1e-20f);

		XMVECTOR Zero = XMVectorZero();

		XMVECTOR e1 = XMVectorSubtract(V1, V0);
		XMVECTOR e2 = XMVectorSubtract(V2, V0);

		// p = Direction ^ e2;
		XMVECTOR p = XMVector3Cross(Direction, e2);

		// det = e1 * p;
		XMVECTOR det = XMVector3Dot(e1, p);

		XMVECTOR u, v, t;

		if (XMVector3GreaterOrEqual(det, g_RayEpsilon))
		{
			// Determinate is positive (front side of the triangle).
			XMVECTOR s = XMVectorSubtract(Origin, V0);

			// u = s * p;
			u = XMVector3Dot(s, p);

			XMVECTOR NoIntersection = XMVectorLess(u, Zero);
			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorGreater(u, det));

			// q = s ^ e1;
			XMVECTOR q = XMVector3Cross(s, e1);

			// v = Direction * q;
			v = XMVector3Dot(Direction, q);

			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorLess(v, Zero));
			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorGreater(XMVectorAdd(u, v), det));

			// t = e2 * q;
			t = XMVector3Dot(e2, q);

			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorLess(t, Zero));

			if (XMVector4EqualInt(NoIntersection, XMVectorTrueInt()))
			{
				Dist = 0.f;
				return false;
			}
		}
		else if (XMVector3LessOrEqual(det, g_RayNegEpsilon))
		{
			// Determinate is negative (back side of the triangle).
			XMVECTOR s = XMVectorSubtract(Origin, V0);

			// u = s * p;
			u = XMVector3Dot(s, p);

			XMVECTOR NoIntersection = XMVectorGreater(u, Zero);
			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorLess(u, det));

			// q = s ^ e1;
			XMVECTOR q = XMVector3Cross(s, e1);

			// v = Direction * q;
			v = XMVector3Dot(Direction, q);

			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorGreater(v, Zero));
			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorLess(XMVectorAdd(u, v), det));

			// t = e2 * q;
			t = XMVector3Dot(e2, q);

			NoIntersection = XMVectorOrInt(NoIntersection, XMVectorGreater(t, Zero));

			if (XMVector4EqualInt(NoIntersection, XMVectorTrueInt()))
			{
				Dist = 0.f;
				return false;
			}
		}
		else
		{
			// Parallel ray.
			Dist = 0.f;
			return false;
		}

		t = XMVectorDivide(t, det);

		const XMVECTOR invdet = XMVectorReciprocal(det);
		XMStoreFloat(&bary.x, u * invdet);
		XMStoreFloat(&bary.y, v * invdet);

		// Store the x-component to *pDist
		XMStoreFloat(&Dist, t);

		if (Dist > TMax || Dist < TMin)
			return false;

		return true;
	}
}