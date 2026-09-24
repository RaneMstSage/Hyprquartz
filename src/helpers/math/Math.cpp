#include "Math.hpp"
#include "../memory/Memory.hpp"
#include "../../macros.hpp"

#include <unordered_map>
#include <array>
#include <cmath>

using namespace Math;

// FIXME: expose in hu
static std::unordered_map<eTransform, Mat3x3> transforms = {
	{HYPRUTILS_TRANSFORM_NORMAL, std::array<float, 9>{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_90, std::array<float, 9>{0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_180, std::array<float, 9>{-1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_270, std::array<float, 9>{0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_FLIPPED, std::array<float, 9>{-1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_FLIPPED_90, std::array<float, 9>{0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_FLIPPED_180, std::array<float, 9>{1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
	{HYPRUTILS_TRANSFORM_FLIPPED_270, std::array<float, 9>{0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
};

eTransform Math::cgRotationToHyprutils(double degrees)
{
	switch (sc<int>(degrees))
	{
		case 90: return eTransform::HYPRUTILS_TRANSFORM_90;
		case 180: return eTransform::HYPRUTILS_TRANSFORM_180;
		case 270: return eTransform::HYPRUTILS_TRANSFORM_270;
		default: break;
	}
	return eTransform::HYPRUTILS_TRANSFORM_NORMAL;
}

eTransform Math::invertTransform(eTransform tr)
{
	if ((tr & HYPRUTILS_TRANSFORM_90) && !(tr & HYPRUTILS_TRANSFORM_FLIPPED))
		tr = sc<eTransform>(tr ^ sc<int>(HYPRUTILS_TRANSFORM_180));

	return tr;
}

static bool matEq(const Mat3x3& a, const Mat3x3& b)
{
	for (size_t i = 0; i < 9; ++i)
	{
		const float Δ = std::fabs(a.getMatrix()[i] - b.getMatrix()[i]);
		if (Δ > 1e-6) // eps
			return false;
	}
	return true;
}

static eTransform composeInternal(eTransform a, eTransform b)
{
	const auto & A		= transforms.at(a);
	const auto & B		= transforms.at(b);
	const auto	 RESULT = Mat3x3{A}.multiply(B);

	for (const auto& [t, M] : transforms)
	{
		if (matEq(M, RESULT))
			return t;
	}

	return eTransform::HYPRUTILS_TRANSFORM_NORMAL;
}

eTransform Math::composeTransform(eTransform a, eTransform b)
{
	static std::array<std::array<eTransform, 8>, 8> lookup;
	static bool										once = true;

	if (once)
	{
		once = false;

		// bake the composition table
		static_assert(HYPRUTILS_TRANSFORM_FLIPPED_270 == 7);
		for (size_t i = 0; i <= HYPRUTILS_TRANSFORM_FLIPPED_270 /* 7 */; ++i)
		{
			for (size_t j = 0; j <= HYPRUTILS_TRANSFORM_FLIPPED_270 /* 7 */; ++j)
			{
				lookup[i][j] = composeInternal(sc<eTransform>(i), sc<eTransform>(j));
			}
		}
	}

	RASSERT(a >= HYPRUTILS_TRANSFORM_NORMAL && a <= HYPRUTILS_TRANSFORM_FLIPPED_270, "Invalid transform a in composeTransform");
	RASSERT(b >= HYPRUTILS_TRANSFORM_NORMAL && b <= HYPRUTILS_TRANSFORM_FLIPPED_270, "Invalid transform b in composeTransform");

	return lookup[a][b];
}

Vector2D Math::transformNormalized(const Vector2D &point, eTransform transform)
{
	return CBox{point, {0, 0}}.transform(transform, 1, 1).pos();
}

Vector2D Math::mapNormalizedToBox(const Vector2D &point, const CBox &box, eTransform transform)
{
	return box.pos() + box.size() * transformNormalized(point, transform);
}